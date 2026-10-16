/*
 * physics.c — Neon Table Tennis physics implementation
 * =====================================================
 *
 * This file implements the complete physics simulation for one match point of
 * Neon Table Tennis.  The model is intentionally lightweight: it runs on the
 * main thread inside the 60 Hz render loop, so all integration is done with a
 * single Euler step per frame.
 *
 * --------------------------------------------------------------------------
 * Physical model overview
 * --------------------------------------------------------------------------
 *
 * 1. Ball forces (IntegrateBall)
 *    Three accelerations act on the ball every tick:
 *
 *    a) Gravity
 *         a_gravity = (0, -g, 0)
 *       where g = 9.81 m/s².
 *
 *    b) Quadratic (Newtonian) air drag
 *         a_drag = -c * |v| * v
 *       The drag coefficient c is small (≈ 0.018) so the ball behaves like a
 *       regulation 40 mm celluloid ball in air.  The |v|*v term makes drag
 *       proportional to the square of speed (realistic for turbulent flow).
 *
 *    c) Magnus effect (topspin / backspin / sidespin)
 *         a_magnus = k * (ω × v)
 *       ω is the spin vector (rad/s), v is the ball velocity, and k is the
 *       Magnus coefficient (≈ 0.0022).  The cross-product produces a force
 *       perpendicular to both the spin axis and the velocity direction, which
 *       bends the ball's path in the correct physical sense.
 *
 * 2. Integration scheme
 *    Semi-implicit (symplectic) Euler:
 *         v_{n+1} = v_n + a(v_n) * dt          ← update velocity first
 *         x_{n+1} = x_n + v_{n+1} * dt         ← then position with new v
 *    This conserves energy better than explicit Euler at the same step size
 *    and is trivially cheap to compute.
 *
 * 3. Collisions
 *    Table surface  — plane at y = tableY; coefficient of restitution 0.76,
 *                     friction 0.91 (X) / 0.97 (Z).
 *    Net            — thin slab at |z| < 0.075; top of visual net at
 *                     tableY + 0.56.  Balls that strike the net lose most
 *                     forward momentum and bounce upward.
 *    Paddle         — AABB overlap test on the XY face; on contact, velocity
 *                     is redirected and spin is accumulated from the offset.
 *    Out of bounds  — detected once the ball centre exits the table footprint
 *                     by a small margin; scoring is awarded to the opponent of
 *                     the last hitter.
 *
 * 4. AI control (PhysicsUpdate)
 *    The AI calls PhysicsPredictLanding to find where the ball will land, adds
 *    a difficulty-scaled random error, then moves its paddle toward that X
 *    position at a speed capped by difficulty * MAX_AI_SPEED.  This gives
 *    the AI a realistic bounded reaction: at difficulty 0 it often misses; at
 *    difficulty 1 it is nearly perfect.
 *
 * --------------------------------------------------------------------------
 * Coordinate system: +X right, +Y up, +Z toward player; net at z = 0.
 * --------------------------------------------------------------------------
 */

#include "physics.h"
#include "math_3d.h"
#include <math.h>
#include <string.h>  /* memset */

/* -------------------------------------------------------------------------
 * Compile-time constants
 * ------------------------------------------------------------------------- */

/* Regulation table-tennis ball diameter is 40 mm → radius 20 mm = 0.020 m.
 * We use a slightly larger value (0.105 m) for comfortable gameplay feel. */
#define BALL_R   0.105f

/* Table dimensions in metres (official ITTF: 2.74 m × 1.525 m, scaled up
 * to fill a 3-D scene comfortably). */
#define TABLE_W  5.0f   /* Full width  (X span) */
#define TABLE_L  9.0f   /* Full length (Z span) */

/* Maximum lateral speed the AI paddle can move (m/s).  Scaled by difficulty
 * in PhysicsUpdate so the AI cannot teleport at low difficulty. */
#define MAX_AI_SPEED  3.2f

/* =========================================================================
 * PhysicsInit
 * ===========
 * Reset every field to a known starting state, set table geometry and
 * physical constants, place paddles at their home positions, and fire the
 * first serve toward the player.
 *
 * The table surface (tableY) is 1.15 m above the world origin so the table
 * legs look correct when rendered.  Paddle Y is 1.25 m — just above the
 * table surface — to start at a natural resting height.
 * ========================================================================= */
void PhysicsInit(PhysicsWorld *w)
{
    /* Zero-initialise everything first so no garbage data lingers. */
    memset(w, 0, sizeof(*w));

    /* --- Table geometry ------------------------------------------------- */
    w->tableW = TABLE_W;
    w->tableL = TABLE_L;
    w->tableY = 1.15f;   /* Y coordinate of the table playing surface */

    /* --- Physical coefficients ------------------------------------------ */
    w->gravity = 9.81f;   /* Gravitational acceleration (m/s²)               */
    w->drag    = 0.018f;  /* Quadratic drag coefficient c in a = -c|v|v      */
    w->magnus  = 0.0022f; /* Magnus lift coefficient   k in a = k(ω × v)     */

    /* --- Ball -----------------------------------------------------------  */
    w->ball.radius = BALL_R;

    /* --- Paddle home positions ------------------------------------------ */
    /* Player sits at the positive-Z end of the table; AI at negative Z.
     * Both start centred (x = 0) and at a comfortable wrist height. */
    w->player = (PaddleBody){
        .position   = { 0.0f, 1.55f, 3.45f },  /* Human player's end (tableY + 0.40m) */
        .velocity   = { 0.0f, 0.0f,  0.0f  },
        .halfWidth  = 0.48f,
        .halfHeight = 0.34f
    };

    w->ai = (PaddleBody){
        .position   = { 0.0f, 1.55f, -3.45f }, /* AI's end (tableY + 0.40m) */
        .velocity   = { 0.0f, 0.0f,   0.0f  },
        .halfWidth  = 0.48f,
        .halfHeight = 0.34f
    };

    /* Fire the first serve toward the human player (AI serves). */
    PhysicsServe(w, 1);
}

/* =========================================================================
 * PhysicsServe
 * ============
 * Place the ball just above the server's side of the table and launch it
 * with a fixed upward arc plus a small random lateral wobble for variety.
 *
 *   towardPlayer > 0  →  AI is serving; ball starts at negative Z, travels
 *                         toward positive Z (toward the player).
 *   towardPlayer ≤ 0  →  Player is serving; ball starts at positive Z,
 *                         travels toward negative Z (toward the AI).
 *
 * Serve origin Z:  ±tableL * 0.29
 *   This scales with table length so the serve arc is proportionate
 *   regardless of any future table dimension changes.  0.29 puts the ball
 *   at roughly 30 % of the half-length from the net — a natural toss point.
 * ========================================================================= */
void PhysicsServe(PhysicsWorld *w, int towardPlayer)
{
    /* Sign convention: +1 if ball travels toward player, -1 toward AI. */
    float dir = (towardPlayer > 0) ? 1.0f : -1.0f;

    /* Serve origin: placed in server's back-court at z = ±3.2m */
    float serveZ = -dir * 3.2f;

    /* Reset ball state: positioned 60 cm above table surface */
    w->ball.position = (Vector3){
        0.0f,
        w->tableY + 0.60f,
        serveZ
    };

    /* Velocity: gentle lateral jitter (±0.4 m/s), 2.5 m/s loft, 5.8 m/s forward drive.
     * This creates a clean arc that clears the net by ~28 cm, bounces on the
     * opponent's half, and arrives at the opponent's paddle at waist height. */
    w->ball.velocity = (Vector3){
        (float)GetRandomValue(-40, 40) / 100.0f,
        2.5f,
        dir * 5.8f
    };

    /* No spin on initial serve toss — spin accumulates from paddle contact. */
    w->ball.spin = (Vector3){ 0.0f, 0.0f, 0.0f };

    w->ball.inPlay  = true;
    w->serveTimer   = 0.0f;

    /* Record who served last and reset court bounce tracking. */
    w->lastHitter            = (towardPlayer > 0) ? 2 : 1;  /* 2 = AI, 1 = player */
    w->bouncesOnReceiverSide = 0;
    w->bouncesOnHitterSide   = 0;
    w->netHit                = false;
}

/* =========================================================================
 * IntegrateBall  (private)
 * ========================
 * Advance the ball's velocity and position by one time step dt using
 * semi-implicit Euler integration.
 *
 * Forces applied (all in world space):
 *
 *   1. Gravity:  a_g = (0, -g, 0)
 *
 *   2. Quadratic drag:
 *        a_drag = -c * |v| * v
 *      This form is the correct low-Mach aerodynamic drag in three
 *      dimensions.  It decelerates the ball along its direction of travel
 *      proportionally to the square of its speed.
 *
 *   3. Magnus effect:
 *        a_magnus = k * (ω × v)
 *      The cross product ω × v produces a vector perpendicular to both the
 *      spin axis and the velocity, directed away from the low-pressure side
 *      of the spinning ball.  This creates topspin curve (ball dips) and
 *      backspin float (ball rises) naturally from the same formula.
 *
 * Integration:
 *   v_{n+1} = v_n + (a_g + a_drag + a_magnus) * dt   ← velocity first
 *   x_{n+1} = x_n + v_{n+1} * dt                     ← position with new v
 *
 * Spin decay:
 *   ω_{n+1} = ω_n * (1 - 0.06 * dt)
 *   This models aerodynamic torque that gradually slows the spin over time.
 *   At dt = 1/60, the factor is ≈ 0.999, so spin persists for several seconds.
 * ========================================================================= */
static void IntegrateBall(PhysicsWorld *w, float dt)
{
    BallBody *b = &w->ball;

    /* --- Compute Magnus acceleration: a_magnus = k * (ω × v) ----------- */
    /* V3Cross(spin, velocity) gives the direction of the Magnus force.
     * Scaling by w->magnus (k) gives the acceleration magnitude in m/s². */
    Vector3 aMagnus = V3Scale(V3Cross(b->spin, b->velocity), w->magnus);

    /* --- Compute quadratic drag: a_drag = -c * |v| * v ------------------ */
    /* V3Len gives scalar speed |v|.  Multiplying the velocity vector by the
     * scalar -c*|v| produces the drag acceleration opposing motion. */
    float   speed  = V3Len(b->velocity);
    Vector3 aDrag  = V3Scale(b->velocity, -w->drag * speed);

    /* --- Gravity --------------------------------------------------------- */
    Vector3 aGrav  = (Vector3){ 0.0f, -w->gravity, 0.0f };

    /* --- Total acceleration --------------------------------------------- */
    Vector3 acc = V3Add(aGrav, V3Add(aMagnus, aDrag));

    /* --- Semi-implicit Euler: update velocity BEFORE position ------------ */
    b->velocity = V3Add(b->velocity, V3Scale(acc, dt));
    b->position = V3Add(b->position, V3Scale(b->velocity, dt));

    /* --- Spin decay: exponential decay via first-order approximation ----- */
    b->spin = V3Scale(b->spin, 1.0f - 0.06f * dt);
}

/* =========================================================================
 * PaddleContact  (private)
 * ========================
 * Axis-aligned bounding-box (AABB) overlap test between the ball and one
 * paddle.
 *
 * The paddle occupies the XY slab centred on p->position.  The test checks
 * three independent axis separations (Separating Axis Theorem for AABBs):
 *
 *   Z axis (depth):   |ball.z - paddle.z| < depthTolerance
 *     depthTolerance (0.22 m) is chosen to be larger than the ball diameter
 *     so that a fast ball moving through the paddle face in one tick is
 *     still detected.  This is a form of continuous collision detection on
 *     the face-normal axis only.
 *
 *   X axis (lateral): |ball.x - paddle.x| < halfWidth + ball.radius
 *     Standard AABB overlap — sum of half-extents > separation means overlap.
 *
 *   Y axis (height):  |ball.y - paddle.y| < halfHeight + ball.radius
 *     Same as X.
 *
 * Bug fixed: the original code passed a separate hardcoded frontZ parameter
 * (3.12f) that did not match the paddle's actual Z position (3.45f set in
 * PhysicsInit).  The Z coordinate is now taken directly from p->position.z,
 * so the test is always consistent with the paddle's current state.
 *
 * Parameters:
 *   b  Pointer to the ball being tested.
 *   p  Pointer to the paddle body being tested against.
 *
 * Returns true if the ball overlaps the paddle face, false otherwise.
 * ========================================================================= */
static bool PaddleContact(const BallBody *b, const PaddleBody *p)
{
    /* Z depth check: 35 cm tolerance prevents tunneling at high velocities. */
    if (fabsf(b->position.z - p->position.z) > 0.35f)
        return false;

    /* X lateral check: ball centre within halfWidth + radius + generous margin. */
    if (fabsf(b->position.x - p->position.x) > p->halfWidth + b->radius + 0.12f)
        return false;

    /* Y height check: ball centre within halfHeight + radius + generous margin. */
    if (fabsf(b->position.y - p->position.y) > p->halfHeight + b->radius + 0.12f)
        return false;

    return true;
}

/* =========================================================================
 * ApplyPaddleHit  (private)
 * =========================
 * Redirect the ball's velocity and add spin when a paddle contact is
 * confirmed.  This models the moment of impact:
 *
 * Contact offset
 * --------------
 * The ball's position relative to the paddle centre is normalised to [-1, 1]
 * on each axis:
 *   offX = clamp((ball.x - paddle.x) / halfWidth,  -1, 1)
 *   offY = clamp((ball.y - paddle.y) / halfHeight, -1, 1)
 *
 * A ball hitting the edge of the paddle deflects to the side more strongly
 * than one hitting the sweet spot near the centre.  This matches real
 * table-tennis deflection physics.
 *
 * Shot power
 * ----------
 *   power = 7.0 + 3.8 * clamp(charge, 0, 1)
 * Minimum 7 m/s; maximum 10.8 m/s with a fully charged smash.
 *
 * Velocity components after impact
 * ---------------------------------
 *   vel.x = offX * 2.6 + paddle.vel.x * 0.28
 *     Lateral: contact offset pushes ball sideways; 28 % of paddle lateral
 *     speed is transferred (realistic for an oblique brush contact).
 *   vel.y = 2.2 + offY * 1.7 + charge * 2.3
 *     Upward: base loft + extra height from hitting the lower edge + smash
 *     power flattens the arc (high charge adds less additional height here
 *     because a smash is deliberately flatter — still an upward component).
 *   vel.z = dir * power
 *     Forward: full power in the direction of play.
 *
 * Spin generation
 * ---------------
 *   Δspin.x =  offY * 8.0   (topspin/backspin from vertical offset)
 *   Δspin.y = -offX * 10.0  (sidespin from horizontal offset)
 *   Δspin.z =  offX * 4.0   (roll)
 * Spin is additive so a previously spinning ball picks up more spin on a
 * brush contact.
 *
 * Depenetration
 * -------------
 * The ball is nudged 12 cm in the hit direction to avoid getting stuck
 * inside the paddle face on the next frame.
 *
 * Parameters:
 *   w       Pointer to the live PhysicsWorld.
 *   p       Pointer to the hitter's PaddleBody.
 *   player  true if the human player is hitting, false if the AI is hitting.
 *   charge  Shot charge level in [0, 1].
 * ========================================================================= */
static void ApplyPaddleHit(PhysicsWorld *w, PaddleBody *p, bool player, float charge)
{
    BallBody *b = &w->ball;

    /* --- Normalised contact offset on the paddle face ------------------- */
    float offX = ClampF((b->position.x - p->position.x) / p->halfWidth,  -1.0f, 1.0f);
    float offY = ClampF((b->position.y - p->position.y) / p->halfHeight, -1.0f, 1.0f);

    /* --- Shot power scales with charge ---------------------------------- */
    /* Range: 7.2 m/s (tap) to 11.2 m/s (full smash). */
    float power = 7.2f + 4.0f * ClampF(charge, 0.0f, 1.0f);

    /* --- Direction of travel after hit ---------------------------------- */
    /* player hits toward negative Z (toward AI); AI hits toward positive Z. */
    float dir = player ? -1.0f : 1.0f;

    /* --- Apply new velocity --------------------------------------------- */
    b->velocity.x = offX * 2.6f                    /* Lateral deflection from offset    */
                  + p->velocity.x * 0.28f;          /* Carry-through of paddle motion    */

    /* Base loft tuned so regular drives clear the net and land neatly on the table,
     * while charged smashes have a flatter trajectory and rely on topspin to dip. */
    float baseLoft = (charge > 0.05f) ? (1.65f + offY * 0.5f - charge * 0.25f)
                                      : (2.25f + offY * 0.8f);
    b->velocity.y = fmaxf(1.40f, baseLoft);
    b->velocity.z = dir * power;                    /* Forward drive                     */

    /* --- Spin accumulation (Magnus effect) ------------------------------- */
    /* Topspin creates a downward aerodynamic force in the direction of flight.
     * With a_y = -dir * spin.x * |vel.z| * k, setting spin.x = dir * topspin
     * guarantees downward Magnus acceleration for both player and AI. */
    float topspin = (player ? (charge * 14.0f + 4.0f) : 3.5f) + offY * 5.0f;
    b->spin.x = dir * topspin;
    b->spin.y = -offX * 8.0f;                       /* Sidespin left/right               */
    b->spin.z =  offX * 3.0f;                       /* Slight roll                       */

    /* --- Depenetration nudge -------------------------------------------- */
    /* Securely place the ball on the active front face of the paddle */
    if (player) {
        b->position.z = p->position.z - (b->radius + 0.08f);
    } else {
        b->position.z = p->position.z + (b->radius + 0.08f);
    }

    /* Record who hit last and reset bounce tracking for this exchange. */
    w->lastHitter            = player ? 1 : 2;
    w->bouncesOnReceiverSide = 0;
    w->bouncesOnHitterSide   = 0;
    w->netHit                = false;
}

/* =========================================================================
 * PhysicsPredictLanding
 * =====================
 * Forward-simulate the ball's trajectory in a temporary local copy (so the
 * live world state is unaffected). Simulates through table bounce to find
 * the point where the ball will cross the AI paddle intercept plane.
 *
 * Parameters:
 *   w        Read-only pointer to the current physics state.
 *   maxTime  Maximum look-ahead in seconds.
 *
 * Returns the predicted landing/intercept position (Vector3).
 * ========================================================================= */
Vector3 PhysicsPredictLanding(const PhysicsWorld *w, float maxTime)
{
    /* Work on a local copy so the live ball is never modified. */
    BallBody b = w->ball;

    /* Sub-step: 120 Hz gives good arc resolution with low CPU cost. */
    const float dtSub = 1.0f / 120.0f;
    float targetZ = w->ai.position.z;

    for (float t = 0.0f; t < maxTime; t += dtSub)
    {
        /* Gravity only — fast numerical look-ahead */
        b.velocity.y -= w->gravity * dtSub;
        b.position = V3Add(b.position, V3Scale(b.velocity, dtSub));

        /* If ball touches the table surface, simulate bounce */
        if (b.position.y <= w->tableY + b.radius && fabsf(b.position.z) <= w->tableL * 0.5f)
        {
            b.position.y = w->tableY + b.radius;
            b.velocity.y = fabsf(b.velocity.y) * 0.76f;
            b.velocity.x *= 0.91f;
            b.velocity.z *= 0.97f;
        }

        /* If ball is travelling toward AI and reaches AI paddle plane, return intercept */
        if (b.velocity.z < 0.0f && b.position.z <= targetZ)
        {
            return (Vector3){ b.position.x, b.position.y, targetZ };
        }
    }

    /* Ball did not cross plane within window; return future position */
    return b.position;
}

/* =========================================================================
 * PhysicsUpdate
 * =============
 * Master per-frame update.  Order of operations:
 *
 *   1. Move the player's paddle to the requested target (clamped to valid
 *      table bounds).  Derive paddle velocity by finite difference.
 *   2. Move the AI paddle toward the predicted landing point with a
 *      difficulty-scaled bounded speed.
 *   3. If ball is in play:
 *       a. Integrate ball motion (gravity + drag + Magnus).
 *       b. Table surface bounce.
 *       c. Paddle contact checks (player then AI).
 *       d. Net collision.
 *       e. Out-of-bounds / scoring check.
 *   4. If ball is not in play, count down the serve timer and fire a new
 *      serve when it reaches zero.
 *
 * AI algorithm (bounded response):
 * ---------------------------------
 *   1. Predict where the ball will land: landX = PhysicsPredictLanding.x
 *   2. Add a difficulty-scaled random error:
 *        targetX = landX + rand(-1,1) * (1 - difficulty) * 0.75
 *      At difficulty = 1.0 the error term is 0 (perfect aim).
 *      At difficulty = 0.0 the AI can be off by ±0.75 m (often misses).
 *   3. Move toward targetX at most MAX_AI_SPEED * difficulty per second:
 *        Δx = clamp(targetX - ai.x, -speed*dt, +speed*dt)
 *      This prevents the AI from teleporting; at low difficulty it cannot
 *      close the gap fast enough even if the target is correct.
 *
 * Parameters: see physics.h for full documentation.
 * ========================================================================= */
void PhysicsUpdate(PhysicsWorld  *w,
                   float          dt,
                   PhysicsInput   in,
                   float          difficulty,
                   int           *playerScore,
                   int           *aiScore,
                   int           *rally)
{
    PaddleBody *p  = &w->player;
    PaddleBody *ai = &w->ai;

    /* -----------------------------------------------------------------------
     * 1. Player paddle: move to requested target, clamped to safe play area.
     * --------------------------------------------------------------------- */
    Vector3 oldPos = p->position;

    /* Clamp requested target to playable table extents */
    float targetX = ClampF(in.target.x,
                           -w->tableW * 0.5f + 0.55f,
                            w->tableW * 0.5f - 0.55f);
    float targetY = ClampF(in.target.y,
                            w->tableY + 0.15f,
                            w->tableY + 1.55f);

    /* Responsive exponential smoothing (38 rad/s filter) ensures silky-smooth
     * 3D paddle motion and prevents frame-rate discrete stepping artifacts. */
    float smoothFactor = 1.0f - expf(-38.0f * dt);
    p->position.x += (targetX - p->position.x) * smoothFactor;
    p->position.y += (targetY - p->position.y) * smoothFactor;

    /* Finite-difference paddle velocity: v = Δx / dt (used in hit transfer). */
    float safeDt   = (dt > 0.0f) ? dt : 1.0f;  /* Guard against dt = 0 */
    p->velocity    = V3Scale(V3Sub(p->position, oldPos), 1.0f / safeDt);

    /* -----------------------------------------------------------------------
     * 2. AI paddle: predict intercept and bounded movement response.
     * --------------------------------------------------------------------- */
    float aiTargetX = 0.0f;

    if (w->ball.inPlay && w->ball.velocity.z < 0.0f)
    {
        /* Ball is coming toward AI: predict intercept position at AI paddle plane */
        Vector3 intercept = PhysicsPredictLanding(w, 1.7f);

        /* Add random positional error inversely proportional to difficulty */
        float errorScale = (float)GetRandomValue(-100, 100) / 100.0f;
        aiTargetX = intercept.x + errorScale * (1.0f - difficulty) * 0.65f;
    }
    else
    {
        /* Ball moving away or dead: AI recovers toward center */
        aiTargetX = 0.0f;
    }

    /* Move toward aiTargetX at bounded speed scaled by difficulty */
    float maxStep   = MAX_AI_SPEED * (0.60f + 0.40f * difficulty) * dt;
    float aiDeltaX  = ClampF(aiTargetX - ai->position.x, -maxStep, maxStep);
    ai->position.x += aiDeltaX;

    /* Clamp AI paddle to table bounds. */
    ai->position.x = ClampF(ai->position.x,
                             -w->tableW * 0.5f + 0.55f,
                              w->tableW * 0.5f - 0.55f);

    /* AI tracks ball height when approaching; recovers to ready height otherwise */
    if (w->ball.inPlay && w->ball.velocity.z < 0.0f) {
        float targetAiY = ClampF(w->ball.position.y, w->tableY + 0.20f, w->tableY + 0.65f);
        ai->position.y += (targetAiY - ai->position.y) * fminf(1.0f, dt * 14.0f);
    } else {
        float readyY = w->tableY + 0.40f;
        ai->position.y += (readyY - ai->position.y) * fminf(1.0f, dt * 8.0f);
    }

    /* -----------------------------------------------------------------------
     * 3. Ball physics (only while ball is in play).
     * --------------------------------------------------------------------- */
    if (w->ball.inPlay)
    {
        /* 3a. Integrate forces over dt. */
        IntegrateBall(w, dt);

        BallBody *b = &w->ball;

        /* -------------------------------------------------------------------
         * 3b. Table surface bounce (Table Tennis rules).
         *
         * Condition:
         *   • Ball's bottom touches or penetrates the table surface.
         *   • Ball is strictly within the table's X and Z boundaries.
         *   • Ball is moving downward (vel.y <= 0) to avoid repeated triggering.
         * ----------------------------------------------------------------- */
        bool inTableX   = fabsf(b->position.x) <= (w->tableW * 0.5f);
        bool inTableZ   = fabsf(b->position.z) <= (w->tableL * 0.5f);
        bool atTableY   = (b->position.y - b->radius <= w->tableY + 0.025f)
                       && (b->position.y + b->radius >= w->tableY - 0.06f);
        bool movingDown = (b->velocity.y <= 0.0f);

        if (inTableX && inTableZ && atTableY && movingDown)
        {
            /* De-penetrate: place ball on surface */
            b->position.y = w->tableY + b->radius;

            /* Bounce: reflect y velocity with restitution 0.76 */
            b->velocity.y = fabsf(b->velocity.y) * 0.76f;

            /* Surface friction on horizontal components */
            b->velocity.x *= 0.91f;
            b->velocity.z *= 0.97f;

            /* Check which court side the ball touched:
             * Player court: z > 0, AI court: z < 0 */
            bool touchedPlayerCourt = (b->position.z > 0.0f);
            bool hitterIsPlayer     = (w->lastHitter == 1);

            if (hitterIsPlayer == touchedPlayerCourt)
            {
                /* Ball bounced on the hitter's OWN court! (Fault in TT) */
                w->bouncesOnHitterSide++;
                if (hitterIsPlayer) {
                    (*aiScore)++;
                } else {
                    (*playerScore)++;
                }
                w->ball.inPlay = false;
                w->serveTimer  = 0.70f;
            }
            else
            {
                /* Ball bounced on the receiver's court! */
                w->bouncesOnReceiverSide++;
                if (w->bouncesOnReceiverSide == 1)
                {
                    /* Legal first bounce on receiver's side */
                    (*rally)++;
                }
                else
                {
                    /* Double bounce! Receiver failed to return in time */
                    if (hitterIsPlayer) {
                        (*playerScore)++;
                    } else {
                        (*aiScore)++;
                    }
                    w->ball.inPlay = false;
                    w->serveTimer  = 0.70f;
                }
            }
        }

        /* -------------------------------------------------------------------
         * 3c. Paddle contact checks.
         * ----------------------------------------------------------------- */
        bool playerInRange = PaddleContact(b, p);
        bool aiInRange     = PaddleContact(b, ai);

        if (w->ball.inPlay && playerInRange
            && b->velocity.z > 0.0f       /* Ball travelling toward player  */
            && w->lastHitter != 1)         /* Player hasn't just hit it      */
        {
            /* Check for illegal volley: in Table Tennis, hitting the ball over
             * the table before it has bounced on your court is an obstruction fault. */
            bool overTable = (fabsf(b->position.x) <= w->tableW * 0.5f) &&
                             (fabsf(b->position.z) <= w->tableL * 0.5f);
            if (w->bouncesOnReceiverSide == 0 && overTable)
            {
                /* Volley fault: AI scores */
                (*aiScore)++;
                w->ball.inPlay = false;
                w->serveTimer  = 0.70f;
            }
            else
            {
                float power = in.smash ? in.charge : (in.hit ? 0.40f : 0.0f);
                ApplyPaddleHit(w, p, true, power);
            }
        }
        else if (w->ball.inPlay && aiInRange
                 && b->velocity.z < 0.0f  /* Ball travelling toward AI      */
                 && w->lastHitter != 2)    /* AI hasn't just hit it          */
        {
            bool overTable = (fabsf(b->position.x) <= w->tableW * 0.5f) &&
                             (fabsf(b->position.z) <= w->tableL * 0.5f);
            if (w->bouncesOnReceiverSide == 0 && overTable)
            {
                /* Volley fault: Player scores */
                (*playerScore)++;
                w->ball.inPlay = false;
                w->serveTimer  = 0.70f;
            }
            else
            {
                ApplyPaddleHit(w, ai, false, 0.05f + difficulty * 0.22f);
            }
        }

        /* -------------------------------------------------------------------
         * 3d. Net collision & de-penetration.
         * ----------------------------------------------------------------- */
        float netHalfThick = 0.05f;
        float netTop       = w->tableY + 0.255f;
        bool inNetSlab     = (fabsf(b->position.z) < netHalfThick + b->radius);
        bool belowNetTop   = (b->position.y < netTop);

        if (w->ball.inPlay && inNetSlab && belowNetTop)
        {
            w->netHit = true;
            if (b->velocity.z > 0.0f)
            {
                /* Travelling toward player: de-penetrate back to AI side */
                b->position.z = -netHalfThick - b->radius - 0.02f;
                b->velocity.z = -fabsf(b->velocity.z) * 0.40f;
                b->velocity.y =  fabsf(b->velocity.y) * 0.35f + 0.55f;
            }
            else if (b->velocity.z < 0.0f)
            {
                /* Travelling toward AI: de-penetrate back to player side */
                b->position.z =  netHalfThick + b->radius + 0.02f;
                b->velocity.z =  fabsf(b->velocity.z) * 0.40f;
                b->velocity.y =  fabsf(b->velocity.y) * 0.35f + 0.55f;
            }
        }

        /* -------------------------------------------------------------------
         * 3e. Out-of-bounds, dead-ball & scoring check (Table Tennis rules).
         * ----------------------------------------------------------------- */
        if (w->ball.inPlay)
        {
            bool outX   = fabsf(b->position.x) > w->tableW * 0.52f;
            bool outZ   = fabsf(b->position.z) > w->tableL * 0.55f;
            bool outY   = b->position.y < (w->tableY - 0.25f);
            bool out    = outX || outZ || outY;

            /* Dead ball check: ball is on table with negligible kinetic energy */
            bool ballDead = inTableX && inTableZ &&
                            (b->position.y <= w->tableY + b->radius + 0.04f) &&
                            (V3Len(b->velocity) < 0.40f);

            if (out || ballDead)
            {
                bool hitterIsPlayer = (w->lastHitter == 1);

                if (w->bouncesOnReceiverSide >= 1)
                {
                    /* Ball bounced legally on receiver's court, then went out or died!
                     * Receiver failed to return it -> Hitter wins the point! */
                    if (hitterIsPlayer) {
                        (*playerScore)++;
                    } else {
                        (*aiScore)++;
                    }
                }
                else
                {
                    /* Ball never touched receiver's court!
                     * Hitter hit it out of bounds or into net -> Receiver wins point! */
                    if (hitterIsPlayer) {
                        (*aiScore)++;
                    } else {
                        (*playerScore)++;
                    }
                }

                w->ball.inPlay  = false;
                w->serveTimer   = 0.70f;
            }
        }
    }
    else
    {
        /* -----------------------------------------------------------------------
         * 4. Serve timer: count down and fire a new serve when expired.
         *
         * Table Tennis Service Rule:
         *   - Alternates every 2 points during regulation play.
         *   - Alternates every 1 point when in deuce (score >= target - 1).
         * --------------------------------------------------------------------- */
        w->serveTimer -= dt;

        if (w->serveTimer <= 0.0f)
        {
            int totalPoints = *playerScore + *aiScore;
            int target = (w->targetScore > 0) ? w->targetScore : 11;
            int towardPlayer;

            if (*playerScore >= (target - 1) && *aiScore >= (target - 1)) {
                /* Deuce: service alternates every point */
                towardPlayer = ((totalPoints % 2) == 0) ? 1 : -1;
            } else {
                /* Regulation: service alternates every 2 points */
                towardPlayer = (((totalPoints / 2) % 2) == 0) ? 1 : -1;
            }

            PhysicsServe(w, towardPlayer);
        }
    }
}
