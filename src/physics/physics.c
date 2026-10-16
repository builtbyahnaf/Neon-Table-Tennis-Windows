/*
 * physics.c — Neon Table Tennis physics implementation
 * =====================================================
 *
 * Coordinate system: +X right, +Y up, +Z toward player; net at z = 0.
 * Player court: z > 0.  AI court: z < 0.  Table surface at y = tableY.
 *
 * DESIGN PRINCIPLES:
 *   1. Rallies continue effortlessly for 5-20+ consecutive exchanges.
 *   2. The Right-Click Energy Meter performs a fast, thrilling SMASH that cleanly
 *      clears the net, lands on the opponent's court, and stays in play.
 *   3. The ball NEVER vanishes on the player's or AI's own table.
 *   4. Points end ONLY on genuine misses: double-bounce on receiver's court, or
 *      ball exiting out-of-bounds past table margins.
 */

#include "physics.h"
#include "math_3d.h"
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define BALL_R        0.105f
#define TABLE_W       5.0f
#define TABLE_L       9.0f
#define HALF_W        (TABLE_W * 0.5f)
#define HALF_L        (TABLE_L * 0.5f)
#define MAX_AI_SPEED  5.5f

/* =========================================================================
 * PhysicsInit
 * ========================================================================= */
void PhysicsInit(PhysicsWorld *w)
{
    memset(w, 0, sizeof(*w));

    w->tableW   = TABLE_W;
    w->tableL   = TABLE_L;
    w->tableY   = 1.15f;

    w->gravity  = 9.81f;
    w->drag     = 0.012f;    /* Gentle drag preserves forward momentum */
    w->magnus   = 0.0012f;

    w->ball.radius = BALL_R;

    /* Paddle positions at z = ±3.45m */
    w->player = (PaddleBody){
        .position   = { 0.0f, 1.50f,  3.45f },
        .velocity   = { 0.0f, 0.0f,   0.0f  },
        .halfWidth  = 0.52f,
        .halfHeight = 0.38f
    };
    w->ai = (PaddleBody){
        .position   = { 0.0f, 1.50f, -3.45f },
        .velocity   = { 0.0f, 0.0f,   0.0f  },
        .halfWidth  = 0.52f,
        .halfHeight = 0.38f
    };

    PhysicsServe(w, 1);   /* AI serves first */
}

/* =========================================================================
 * PhysicsServe
 *
 * Spawns ball from server's side, aligned with server's paddle X, with a
 * clean arc that clears the net by ~35cm and bounces smoothly on receiver's court.
 * ========================================================================= */
void PhysicsServe(PhysicsWorld *w, int towardPlayer)
{
    float dir    = (towardPlayer > 0) ? 1.0f : -1.0f;
    float serveZ = -dir * 2.80f;

    /* Align serve X with server's paddle position */
    float serveX = (towardPlayer > 0) ? w->ai.position.x : w->player.position.x;
    serveX = ClampF(serveX, -HALF_W * 0.45f, HALF_W * 0.45f);

    w->ball.position = (Vector3){ serveX, w->tableY + 0.50f, serveZ };
    w->ball.velocity = (Vector3){
        (float)GetRandomValue(-20, 20) / 100.0f,   /* ±0.20 m/s gentle lateral jitter */
        2.50f,                                       /* reliable arc clearance */
        dir * 6.50f                                  /* smooth forward drive */
    };
    w->ball.spin   = (Vector3){ 0.0f, 0.0f, 0.0f };
    w->ball.inPlay = true;
    w->serveTimer  = 0.0f;

    w->lastHitter            = (towardPlayer > 0) ? 2 : 1;
    w->bouncesOnReceiverSide = 0;
    w->bouncesOnHitterSide   = 0;
    w->netHit                = false;
}

/* =========================================================================
 * IntegrateBall  (private)
 * ========================================================================= */
static void IntegrateBall(PhysicsWorld *w, float dt)
{
    BallBody *b = &w->ball;

    Vector3 aMagnus = V3Scale(V3Cross(b->spin, b->velocity), w->magnus);
    float   speed   = V3Len(b->velocity);
    Vector3 aDrag   = V3Scale(b->velocity, -w->drag * speed);
    Vector3 aGrav   = (Vector3){ 0.0f, -w->gravity, 0.0f };
    Vector3 acc     = V3Add(aGrav, V3Add(aMagnus, aDrag));

    b->velocity = V3Add(b->velocity, V3Scale(acc, dt));
    b->position = V3Add(b->position, V3Scale(b->velocity, dt));
    b->spin     = V3Scale(b->spin, 1.0f - 0.03f * dt);
}

/* =========================================================================
 * PaddleContact  (private)
 * ========================================================================= */
static bool PaddleContact(const BallBody *b, const PaddleBody *p)
{
    if (fabsf(b->position.z - p->position.z) > 0.32f)   return false;
    if (fabsf(b->position.x - p->position.x) > p->halfWidth  + b->radius + 0.10f) return false;
    if (fabsf(b->position.y - p->position.y) > p->halfHeight + b->radius + 0.10f) return false;
    return true;
}

/* =========================================================================
 * ApplyPaddleHit  (private)
 *
 * Applies speed and loft calibrated for guaranteed net clearance and consistent
 * single bounce on the opponent's court.
 *
 *   charge = 0.00  →  Auto-return / drive :  8.8 m/s forward, 2.30 m/s loft
 *   charge = 0.40  →  Left-click drive    : 10.3 m/s forward, 2.04 m/s loft
 *   charge = 1.00  →  Right-click smash   : 12.6 m/s forward, 1.65 m/s loft
 * ========================================================================= */
static void ApplyPaddleHit(PhysicsWorld *w, PaddleBody *p, bool player, float charge)
{
    BallBody *b = &w->ball;

    float offX = ClampF((b->position.x - p->position.x) / p->halfWidth,  -1.0f, 1.0f);
    float c    = ClampF(charge, 0.0f, 1.0f);
    float dir  = player ? -1.0f : 1.0f;

    /* Forward drive and loft smoothly scaled with charge */
    float speed = 8.8f + 3.8f * c;
    float baseLoft = 2.40f - 0.60f * c;

    /* Upward boost if ball is hit from low height so it always clears net cleanly */
    float lowBoost = 0.0f;
    if (b->position.y < w->tableY + 0.35f) {
        lowBoost = (w->tableY + 0.35f - b->position.y) * 2.0f;
    }
    float loft = baseLoft + lowBoost;

    /* Controlled lateral deflection keeps ball comfortably within table width */
    b->velocity.x = offX * 1.50f + p->velocity.x * 0.12f;
    b->velocity.x = ClampF(b->velocity.x, -2.20f, 2.20f);

    b->velocity.y = loft;
    b->velocity.z = dir * speed;

    /* Moderate spin for nice visual curve */
    float topspin = 2.0f + c * 5.0f;
    b->spin.x = dir * topspin;
    b->spin.y = -offX * 4.0f;
    b->spin.z =  offX * 1.5f;

    /* Depenetrate ball in front of paddle */
    b->position.z = player ? (p->position.z - b->radius - 0.10f)
                           : (p->position.z + b->radius + 0.10f);

    w->lastHitter            = player ? 1 : 2;
    w->bouncesOnReceiverSide = 0;
    w->bouncesOnHitterSide   = 0;
    w->netHit                = false;
}

/* =========================================================================
 * PhysicsPredictLanding
 * ========================================================================= */
Vector3 PhysicsPredictLanding(const PhysicsWorld *w, float maxTime)
{
    BallBody    b      = w->ball;
    const float dtSub  = 1.0f / 120.0f;
    float       targetZ = w->ai.position.z;

    for (float t = 0.0f; t < maxTime; t += dtSub)
    {
        b.velocity.y -= w->gravity * dtSub;
        b.position    = V3Add(b.position, V3Scale(b.velocity, dtSub));

        /* Predict table bounce */
        if (b.position.y <= w->tableY + b.radius && fabsf(b.position.z) <= HALF_L && b.velocity.y <= 0.0f)
        {
            b.position.y  = w->tableY + b.radius;
            b.velocity.y  = fabsf(b.velocity.y) * 0.88f;
            b.velocity.z *= 0.98f;
        }

        if (b.velocity.z < 0.0f && b.position.z <= targetZ)
            return (Vector3){ b.position.x, b.position.y, targetZ };
    }

    return b.position;
}

/* =========================================================================
 * PhysicsUpdate
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
     * 1. Player paddle movement (smooth, responsive tracking).
     * --------------------------------------------------------------------- */
    Vector3 oldPos  = p->position;
    float targetX   = ClampF(in.target.x, -HALF_W + 0.55f, HALF_W - 0.55f);
    float targetY   = ClampF(in.target.y,  w->tableY + 0.12f, w->tableY + 1.65f);
    float sf        = 1.0f - expf(-45.0f * dt);

    p->position.x  += (targetX - p->position.x) * sf;
    p->position.y  += (targetY - p->position.y) * sf;

    float safeDt    = (dt > 1e-6f) ? dt : 1.0f;
    p->velocity     = V3Scale(V3Sub(p->position, oldPos), 1.0f / safeDt);

    /* -----------------------------------------------------------------------
     * 2. AI paddle movement (anticipate landing, smooth interception).
     * --------------------------------------------------------------------- */
    float aiTX = 0.0f;
    float aiTY = w->tableY + 0.35f;

    if (w->ball.inPlay && w->ball.velocity.z < 0.0f)
    {
        Vector3 intercept = PhysicsPredictLanding(w, 2.0f);
        /* Small error scaled with difficulty (at diff 0.65, error is tiny ±0.08m) */
        float   errScale  = (float)GetRandomValue(-100, 100) / 100.0f;
        aiTX = intercept.x + errScale * (1.0f - difficulty) * 0.25f;
        aiTY = ClampF(intercept.y, w->tableY + 0.18f, w->tableY + 0.75f);
    }

    float maxStep   = MAX_AI_SPEED * (0.65f + 0.35f * difficulty) * dt;
    float aiDeltaX  = ClampF(aiTX - ai->position.x, -maxStep, maxStep);
    ai->position.x  = ClampF(ai->position.x + aiDeltaX, -HALF_W + 0.55f, HALF_W - 0.55f);

    float aiYRate   = (w->ball.inPlay && w->ball.velocity.z < 0.0f) ? 20.0f : 8.0f;
    ai->position.y += (aiTY - ai->position.y) * fminf(1.0f, dt * aiYRate);

    /* -----------------------------------------------------------------------
     * 3. Serve countdown when ball is not in play.
     * --------------------------------------------------------------------- */
    if (!w->ball.inPlay)
    {
        w->serveTimer -= dt;
        if (w->serveTimer <= 0.0f)
        {
            int totalPts = *playerScore + *aiScore;
            int target   = (w->targetScore > 0) ? w->targetScore : 11;
            int towardPlayer;

            if (*playerScore >= (target - 1) && *aiScore >= (target - 1))
                towardPlayer = ((totalPts % 2) == 0) ? 1 : -1;
            else
                towardPlayer = (((totalPts / 2) % 2) == 0) ? 1 : -1;

            PhysicsServe(w, towardPlayer);
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * 4a. Integrate ball forces.
     * --------------------------------------------------------------------- */
    IntegrateBall(w, dt);
    BallBody *b = &w->ball;

    /* -----------------------------------------------------------------------
     * 4b. Net collision — physical deflector at z = 0.
     * --------------------------------------------------------------------- */
    {
        float netHalfThick = 0.045f;
        float netTop       = w->tableY + 0.16f;
        bool  inNetSlab    = (fabsf(b->position.z) <= netHalfThick + b->radius);
        bool  belowNetTop  = (b->position.y <= netTop);
        bool  onTable      = (fabsf(b->position.x) <= HALF_W);

        if (inNetSlab && belowNetTop && onTable)
        {
            w->netHit = true;
            /* Deflect backward softly */
            if (b->velocity.z > 0.0f) {
                b->position.z = -(netHalfThick + b->radius + 0.02f);
                b->velocity.z = -fabsf(b->velocity.z) * 0.30f;
            } else {
                b->position.z =  netHalfThick + b->radius + 0.02f;
                b->velocity.z =  fabsf(b->velocity.z) * 0.30f;
            }
            b->velocity.y = fabsf(b->velocity.y) * 0.30f + 0.35f;
        }
    }

    /* -----------------------------------------------------------------------
     * 4c. Table surface bounce.
     *
     * In Table Tennis:
     *   • Ball bounces on receiver's side → rally continues.
     *   • Second bounce on receiver's side before return → point scored!
     * --------------------------------------------------------------------- */
    {
        bool inTableX   = fabsf(b->position.x) <= HALF_W;
        bool inTableZ   = fabsf(b->position.z) <= HALF_L;
        bool atTableY   = (b->position.y - b->radius <= w->tableY + 0.035f)
                       && (b->position.y              >= w->tableY - 0.12f);
        bool movingDown = (b->velocity.y <= 0.05f);

        if (inTableX && inTableZ && atTableY && movingDown)
        {
            /* De-penetrate */
            b->position.y = w->tableY + b->radius;

            /* Lively table bounce */
            b->velocity.y = fabsf(b->velocity.y) * 0.88f;
            b->velocity.x *= 0.92f;
            b->velocity.z *= 0.98f;

            /* Check if bounce landed on receiver's court */
            bool isReceiverSide = (w->lastHitter == 1 && b->position.z < -0.05f) ||
                                  (w->lastHitter == 2 && b->position.z >  0.05f);

            if (isReceiverSide)
            {
                w->bouncesOnReceiverSide++;
                if (w->bouncesOnReceiverSide == 1)
                {
                    (*rally)++;   /* Legal first bounce — rally continues! */
                }
                else
                {
                    /* Double bounce: receiver failed to return in time */
                    if (w->lastHitter == 1) { (*playerScore)++; }
                    else                    { (*aiScore)++;     }
                    w->ball.inPlay = false;
                    w->serveTimer  = 0.85f;
                }
            }
        }
    }

    /* -----------------------------------------------------------------------
     * 4d. Paddle contact checks (auto-return on contact).
     * --------------------------------------------------------------------- */
    if (w->ball.inPlay)
    {
        bool playerInRange = PaddleContact(b, p);
        bool aiInRange     = PaddleContact(b, ai);

        if (playerInRange
            && b->velocity.z > 0.0f       /* Travelling toward player (+Z) */
            && w->lastHitter != 1)         /* Player hasn't just hit it     */
        {
            ApplyPaddleHit(w, p, true, in.charge);
        }
        else if (aiInRange
                 && b->velocity.z < 0.0f  /* Travelling toward AI (-Z)     */
                 && w->lastHitter != 2)    /* AI hasn't just hit it         */
        {
            /* AI returns with clean, playable charge */
            float aiCharge = 0.12f + difficulty * 0.18f;
            ApplyPaddleHit(w, ai, false, aiCharge);
        }
    }

    /* -----------------------------------------------------------------------
     * 4e. Out-of-bounds scoring.
     * --------------------------------------------------------------------- */
    if (w->ball.inPlay)
    {
        bool outX = fabsf(b->position.x) > HALF_W + 0.80f;
        bool outZ = fabsf(b->position.z) > HALF_L + 1.20f;
        bool outY = b->position.y < w->tableY - 0.70f;

        if (outX || outZ || outY)
        {
            bool hitterIsPlayer = (w->lastHitter == 1);

            if (w->bouncesOnReceiverSide >= 1) {
                /* Ball bounced legally on receiver's court, then went out -> hitter scores! */
                if (hitterIsPlayer) { (*playerScore)++; }
                else                { (*aiScore)++;     }
            } else {
                /* Ball never bounced on receiver's court -> receiver scores! */
                if (hitterIsPlayer) { (*aiScore)++;     }
                else                { (*playerScore)++; }
            }

            w->ball.inPlay = false;
            w->serveTimer  = 0.85f;
        }
    }
}
