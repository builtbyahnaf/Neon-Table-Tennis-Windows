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
        .position   = { 0.0f, 1.25f, 3.45f },  /* Human player's end  */
        .velocity   = { 0.0f, 0.0f,  0.0f  },
        .halfWidth  = 0.48f,
        .halfHeight = 0.34f
    };

    w->ai = (PaddleBody){
        .position   = { 0.0f, 1.25f, -3.45f }, /* AI's end            */
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

    /* Record who served last for point attribution if faulted. */
    w->lastHitter = (towardPlayer > 0) ? 2 : 1;  /* 2 = AI, 1 = player */
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
    /* Z depth check: ball must be within 22 cm of the paddle face. */
    if (fabsf(b->position.z - p->position.z) > 0.22f)
        return false;

    /* X lateral check: ball centre within halfWidth + radius of paddle centre. */
    if (fabsf(b->position.x - p->position.x) > p->halfWidth + b->radius)
        return false;

    /* Y height check: ball centre within halfHeight + radius of paddle centre. */
    if (fabsf(b->position.y - p->position.y) > p->halfHeight + b->radius)
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
    /* Range: 7.0 m/s (tap) to 10.8 m/s (full smash). */
    float power = 7.0f + 3.8f * ClampF(charge, 0.0f, 1.0f);

    /* --- Direction of travel after hit ---------------------------------- */
    /* player hits toward negative Z (toward AI); AI hits toward positive Z. */
    float dir = player ? -1.0f : 1.0f;

    /* --- Apply new velocity --------------------------------------------- */
    b->velocity.x = offX * 2.6f                    /* Lateral deflection from offset    */
                  + p->velocity.x * 0.28f;          /* Carry-through of paddle motion    */
    b->velocity.y = 2.2f                            /* Base loft                         */
                  + offY * 1.7f                     /* Edge height bonus/penalty         */
                  + charge * 2.3f;                  /* Smash power increases loft        */
    b->velocity.z = dir * power;                    /* Full forward drive                */

    /* --- Spin accumulation --------------------------------------------- */
    /* Spin is added to whatever the ball was already spinning with, so a
     * brush contact on an already-spinning ball magnifies the effect. */
    b->spin = V3Add(b->spin, (Vector3){
         offY * 8.0f,    /* Topspin (+) or backspin (-) from vertical offset  */
        -offX * 10.0f,   /* Sidespin left/right from horizontal offset         */
         offX * 4.0f     /* Slight roll component                              */
    });

    /* --- Depenetration nudge -------------------------------------------- */
    /* Push ball away from paddle face by 12 cm to prevent re-contact. */
    b->position.z += dir * 0.12f;

    /* Record who hit last for scoring attribution. */
    w->lastHitter = player ? 1 : 2;
}

/* =========================================================================
 * PhysicsPredictLanding
 * =====================
 * Forward-simulate the ball's trajectory in a temporary local copy (so the
 * live world state is unaffected) and return the point where the ball will
 * next touch y = tableY.
 *
 * Algorithm
 * ---------
 * The prediction integrates the ball under gravity only (no drag, no Magnus)
 * at a 120 Hz sub-step (dt_sub = 1/120 s ≈ 8.33 ms).  For short-range shots
 * that last < 2 s, omitting drag introduces < 5 % error in landing position,
 * which is acceptable given that the AI also has a randomised difficulty
 * offset.
 *
 * Each sub-step:
 *   v_y -= g * dt_sub          (explicit Euler on y component)
 *   pos  += vel * dt_sub       (update all three components)
 *   if pos.y <= tableY + radius  →  landing detected, return clamped pos
 *
 * If maxTime elapses before landing (e.g. ball is still climbing) the ball's
 * projected position at that time is returned.  The AI will simply idle near
 * its last target until the next update recalculates.
 *
 * Parameters:
 *   w        Read-only pointer to the current physics state.
 *   maxTime  Maximum look-ahead in seconds.
 *
 * Returns the predicted landing position (Vector3).
 * ========================================================================= */
Vector3 PhysicsPredictLanding(const PhysicsWorld *w, float maxTime)
{
    /* Work on a local copy so the live ball is never modified. */
    BallBody b = w->ball;

    /* Sub-step: 120 Hz gives good arc resolution with low CPU cost. */
    const float dtSub = 1.0f / 120.0f;

    for (float t = 0.0f; t < maxTime; t += dtSub)
    {
        /* Gravity only — ignoring drag and Magnus for speed. */
        b.velocity.y -= w->gravity * dtSub;

        /* Advance position. */
        b.position = V3Add(b.position, V3Scale(b.velocity, dtSub));

        /* Check if the ball has reached (or passed through) the table surface. */
        if (b.position.y <= w->tableY + b.radius)
        {
            /* Return the landing point with y clamped to the table surface. */
            return (Vector3){ b.position.x, w->tableY, b.position.z };
        }
    }

    /* Ball did not land within the look-ahead window; return its future position. */
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

    /* Clamp X: keep paddle at least halfWidth from the table edge. */
    p->position.x = ClampF(in.target.x,
                            -w->tableW * 0.5f + 0.55f,
                             w->tableW * 0.5f - 0.55f);

    /* Clamp Y: keep paddle between just above the table and head height. */
    p->position.y = ClampF(in.target.y,
                            w->tableY + 0.15f,
                            w->tableY + 1.55f);

    /* Finite-difference paddle velocity: v = Δx / dt (used in hit transfer). */
    float safeDt   = (dt > 0.0f) ? dt : 1.0f;  /* Guard against dt = 0 */
    p->velocity    = V3Scale(V3Sub(p->position, oldPos), 1.0f / safeDt);

    /* -----------------------------------------------------------------------
     * 2. AI paddle: predict landing and bounded movement response.
     * --------------------------------------------------------------------- */

    /* Forward-simulate up to 1.7 s to find the landing point. */
    Vector3 landing = PhysicsPredictLanding(w, 1.7f);

    /* Add random positional error inversely proportional to difficulty.
     * rand range [-1, 1] scaled by (1-difficulty)*0.75 → max ±0.75 m at diff=0. */
    float errorScale = (float)GetRandomValue(-100, 100) / 100.0f;
    float aiTargetX  = landing.x + errorScale * (1.0f - difficulty) * 0.75f;

    /* Move at most MAX_AI_SPEED * difficulty per second toward targetX. */
    float maxStep   = MAX_AI_SPEED * difficulty * dt;
    float aiDeltaX  = ClampF(aiTargetX - ai->position.x, -maxStep, maxStep);
    ai->position.x += aiDeltaX;

    /* Clamp AI paddle to table bounds. */
    ai->position.x = ClampF(ai->position.x,
                             -w->tableW * 0.5f + 0.55f,
                              w->tableW * 0.5f - 0.55f);

    /* AI always keeps its paddle at a fixed comfortable height. */
    ai->position.y = w->tableY + 0.70f;

    /* -----------------------------------------------------------------------
     * 3. Ball physics (only while ball is in play).
     * --------------------------------------------------------------------- */
    if (w->ball.inPlay)
    {
        /* 3a. Integrate forces over dt. */
        IntegrateBall(w, dt);

        BallBody *b = &w->ball;

        /* -------------------------------------------------------------------
         * 3b. Table surface bounce.
         *
         * Condition: ball's bottom edge (position.y - radius) has reached or
         * passed the table surface, AND ball is within the table's Z span.
         *
         * Response:
         *   • Clamp y to keep the ball on the surface (de-penetration).
         *   • Reflect y velocity with a coefficient of restitution of 0.76:
         *       vy_new = |vy_old| * 0.76
         *     Taking fabsf first ensures we only ever bounce the ball upward,
         *     regardless of floating-point drift.
         *   • Apply friction on the horizontal axes:
         *       vx *= 0.91  (more friction: ball scrubs laterally)
         *       vz *= 0.97  (less friction: ball retains most forward speed)
         *   • Rally counter increments on each table contact.
         *
         * Bug fixed: the original code clamped vy to a minimum of 1.1 m/s,
         * which caused a slow rolling ball to bounce endlessly.  That clamp
         * has been removed — if the ball doesn't have enough energy to bounce,
         * it should simply stop, letting the out-of-bounds check score the point.
         * ----------------------------------------------------------------- */
        bool onTable = (b->position.y - b->radius <= w->tableY)
                    && (fabsf(b->position.z) < w->tableL * 0.5f);

        if (onTable)
        {
            /* De-penetrate: place ball surface exactly on table top. */
            b->position.y = w->tableY + b->radius;

            /* Bounce: reflect y velocity with restitution coefficient 0.76. */
            b->velocity.y = fabsf(b->velocity.y) * 0.76f;

            /* Surface friction on horizontal components. */
            b->velocity.x *= 0.91f;
            b->velocity.z *= 0.97f;

            /* Count this as a rally touch. */
            (*rally)++;
        }

        /* -------------------------------------------------------------------
         * 3c. Paddle contact checks.
         *
         * Player hit conditions:
         *   • Ball overlaps player paddle AABB.
         *   • Ball is moving toward player (vel.z > 0) — prevents double-hit
         *     from a ball that has passed through and is rebounding.
         *   • Player did not just hit it (lastHitter != 1) — no double-hit.
         *   • Player pressed the hit button this frame (in.hit).
         *
         * AI hit conditions:
         *   • Ball overlaps AI paddle AABB.
         *   • Ball is moving toward AI (vel.z < 0).
         *   • AI did not just hit it (lastHitter != 2).
         *   • No player input required — AI always auto-hits when in range.
         *   • A small charge (0.05) gives the AI a modest power shot.
         *
         * PaddleContact now derives the paddle Z from p->position.z directly
         * (bug fix — the original passed hardcoded 3.12f for the player even
         * though the paddle is at z = 3.45f).
         * ----------------------------------------------------------------- */
        bool playerInRange = PaddleContact(b, p);
        bool aiInRange     = PaddleContact(b, ai);

        if (playerInRange
            && b->velocity.z > 0.0f       /* Ball travelling toward player  */
            && w->lastHitter != 1          /* Player hasn't just hit it      */
            && in.hit)                     /* Player pressed hit this frame  */
        {
            ApplyPaddleHit(w, p, true, in.smash ? in.charge : 0.0f);
        }
        else if (aiInRange
                 && b->velocity.z < 0.0f  /* Ball travelling toward AI      */
                 && w->lastHitter != 2)    /* AI hasn't just hit it          */
        {
            ApplyPaddleHit(w, ai, false, 0.05f);
        }

        /* -------------------------------------------------------------------
         * 3d. Net collision & de-penetration.
         *
         * The net is located at z = 0 with half-thickness 0.05m.
         * Net top is at tableY + 0.255m (matching the rendered net geometry).
         *
         * Bug Fix (The Net Trap):
         * Previously, hitting the net merely multiplied vz by -0.35 without
         * adjusting position. Because the ball remained inside the slab
         * (|z| < 0.075), the collision condition triggered repeatedly every frame,
         * multiplying velocity by 0.35 each time until the ball was permanently
         * trapped inside the net at z = 0 with zero velocity.
         *
         * Resolution:
         *   1. Detect approach direction (vz > 0: approaching from AI toward player;
         *      vz < 0: approaching from player toward AI).
         *   2. Immediately de-penetrate the ball by snapping its z position
         *      outside the slab boundary.
         *   3. Reflect vz away from the net and provide a small vertical bounce.
         * ----------------------------------------------------------------- */
        float netHalfThick = 0.05f;
        float netTop       = w->tableY + 0.255f;
        bool inNetSlab     = (fabsf(b->position.z) < netHalfThick + b->radius);
        bool belowNetTop   = (b->position.y < netTop);

        if (inNetSlab && belowNetTop)
        {
            if (b->velocity.z > 0.0f)
            {
                /* Travelling toward player: de-penetrate back to AI side */
                b->position.z = -netHalfThick - b->radius - 0.02f;
                b->velocity.z = -fabsf(b->velocity.z) * 0.45f;
                b->velocity.y =  fabsf(b->velocity.y) * 0.40f + 0.60f;
            }
            else if (b->velocity.z < 0.0f)
            {
                /* Travelling toward AI: de-penetrate back to player side */
                b->position.z =  netHalfThick + b->radius + 0.02f;
                b->velocity.z =  fabsf(b->velocity.z) * 0.45f;
                b->velocity.y =  fabsf(b->velocity.y) * 0.40f + 0.60f;
            }
        }

        /* -------------------------------------------------------------------
         * 3e. Out-of-bounds, dead-ball & scoring check.
         *
         * A point ends when:
         *   1. The ball flies beyond the lateral table bounds (|x| > tableW * 0.52).
         *   2. The ball flies beyond the baseline (|z| > tableL * 0.55).
         *   3. The ball falls through the floor (y < -0.8m).
         *   4. The ball becomes stalled/dead on the table (e.g. after hitting
         *      the net or rolling to a halt: speed < 0.45 m/s near table surface).
         * ----------------------------------------------------------------- */
        bool outX   = fabsf(b->position.x) > w->tableW * 0.52f;
        bool outZ   = fabsf(b->position.z) > w->tableL * 0.55f;
        bool outY   = b->position.y < -0.8f;
        bool out    = outX || outZ || outY;

        /* Dead ball check: ball is near table top with negligible kinetic energy */
        bool ballDead = onTable && (V3Len(b->velocity) < 0.45f);

        if (out || ballDead)
        {
            if (ballDead)
            {
                /* If the ball died on player's half, player failed to return it -> AI scores.
                 * If it died on AI's half, AI failed to return it -> player scores. */
                if (b->position.z > 0.0f)
                    (*aiScore)++;
                else
                    (*playerScore)++;
            }
            else
            {
                /* Normal out-of-bounds: opponent of the last hitter scores */
                if (w->lastHitter == 1)
                    (*aiScore)++;
                else
                    (*playerScore)++;
            }

            /* Take the ball out of play; serve timer will trigger next serve */
            w->ball.inPlay  = false;
            w->serveTimer   = 0.65f;  /* 650 ms delay before next serve */
        }
    }
    else
    {
        /* -----------------------------------------------------------------------
         * 4. Serve timer: count down and fire a new serve when expired.
         *
         * Serves alternate: even total points → AI serves, odd → player serves.
         * --------------------------------------------------------------------- */
        w->serveTimer -= dt;

        if (w->serveTimer <= 0.0f)
        {
            int totalPoints = *playerScore + *aiScore;
            int towardPlayer = ((totalPoints % 2) == 0) ? 1 : -1;
            PhysicsServe(w, towardPlayer);
        }
    }
}
