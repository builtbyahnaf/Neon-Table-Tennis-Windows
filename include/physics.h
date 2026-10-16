/*
 * physics.h — Neon Table Tennis physics subsystem
 * ================================================
 *
 * This header defines the data model and public API for the rigid-body /
 * particle physics layer of Neon Table Tennis.
 *
 * Coordinate system
 * -----------------
 *   +X  →  right (lateral across table width)
 *   +Y  ↑  up    (height above floor)
 *   +Z  →  toward the human player
 *
 * The net sits exactly at z = 0.  The human player's end of the table is at
 * positive Z; the AI end is at negative Z.  Table surface is at y = tableY.
 *
 * Physics model summary
 * ---------------------
 *   • Ball integration: semi-implicit Euler at the simulation tick rate.
 *   • Forces per tick: gravity, quadratic air drag, Magnus (spin–velocity) lift.
 *   • Collisions: AABB paddle overlap, plane bounce on the table surface,
 *     thin-slab net check, out-of-bounds scoring detection.
 *   • Spin: applied at every paddle hit and decays exponentially each tick.
 *
 * All physical constants are stored in PhysicsWorld so the renderer and audio
 * layers can read them without needing separate #defines.
 */

#ifndef NTT_PHYSICS_H
#define NTT_PHYSICS_H

#include "raylib.h"   /* Vector3 */
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * BallBody
 * --------
 * Represents the ball as a point-mass sphere with angular momentum (spin).
 * The spin vector encodes the axis and magnitude of rotation in rad/s.
 * Magnus acceleration is a_magnus = magnus_k * (spin × velocity).
 * ------------------------------------------------------------------------- */
typedef struct BallBody
{
    Vector3 position;   /* World-space centre of the ball (metres)            */
    Vector3 velocity;   /* Linear velocity vector (m/s)                       */
    Vector3 spin;       /* Angular velocity vector (rad/s); drives Magnus lift */
    float   radius;     /* Ball radius (metres); used in all collision tests   */
    bool    inPlay;     /* True while the ball is live; false in serve delay   */
} BallBody;

/* -------------------------------------------------------------------------
 * PaddleBody
 * ----------
 * Represents a paddle as an axis-aligned bounding box (AABB) centred on
 * 'position'.  The paddle face that the ball can contact is the XY plane at
 * position.z.  halfWidth extends along X, halfHeight extends along Y.
 * ------------------------------------------------------------------------- */
typedef struct PaddleBody
{
    Vector3 position;   /* World-space centre of the paddle face (metres)     */
    Vector3 velocity;   /* Paddle velocity (m/s); transferred to ball on hit  */
    float   halfWidth;  /* Half-extent along X (metres)                       */
    float   halfHeight; /* Half-extent along Y (metres)                       */
} PaddleBody;

/* -------------------------------------------------------------------------
 * PhysicsWorld
 * ------------
 * The single authoritative physics state for an entire match point.
 * Pass a pointer to this struct to every Physics* function.
 * ------------------------------------------------------------------------- */
typedef struct PhysicsWorld
{
    BallBody   ball;          /* The one ball in play                          */
    PaddleBody player;        /* Human player's paddle (positive Z end)        */
    PaddleBody ai;            /* AI paddle (negative Z end)                    */

    /* Table geometry — treat as read-only after PhysicsInit. */
    float tableW;             /* Full table width  (X span, metres)            */
    float tableL;             /* Full table length (Z span, metres)            */
    float tableY;             /* Y coordinate of the table surface (metres)   */

    /* Physical coefficients. */
    float gravity;            /* Gravitational acceleration (m/s²)             */
    float drag;               /* Quadratic drag coefficient  c in a=-c|v|v     */
    float magnus;             /* Magnus lift coefficient     k in a=k(ω×v)     */

    /* Match flow & Table Tennis bounce tracking. */
    float serveTimer;             /* Seconds remaining before the next serve fires */
    int   lastHitter;             /* 1 = player last hit, 2 = AI last hit, 0 = none */
    int   bouncesOnReceiverSide;  /* Number of bounces on receiver's court since last hit */
    int   bouncesOnHitterSide;    /* Number of bounces on hitter's court since last hit   */
    bool  netHit;                 /* True if ball clipped the net on current exchange     */
    int   targetScore;            /* Minimum score to win (used for deuce service cadence)*/
} PhysicsWorld;

/* -------------------------------------------------------------------------
 * PhysicsInput
 * ------------
 * Per-frame control input from the human player, produced by the input/UI
 * layer and passed into PhysicsUpdate each tick.
 * ------------------------------------------------------------------------- */
typedef struct PhysicsInput
{
    Vector3 target;   /* Desired world-space position for the player paddle    */
    bool    hit;      /* True if the player pressed the hit button this frame  */
    bool    smash;    /* True if the player is attempting a smash / power shot */
    float   charge;   /* Charge level in [0, 1]; scales shot power and spin    */
} PhysicsInput;

/* =========================================================================
 * Public API
 * ========================================================================= */

/*
 * PhysicsInit — Reset the world to default starting state.
 *
 *   w   Pointer to the PhysicsWorld to initialise.  All fields are zeroed
 *       first; then table geometry, physical constants, and paddle positions
 *       are set to their design values, and an initial serve is triggered.
 */
void PhysicsInit(PhysicsWorld *w);

/*
 * PhysicsServe — Launch the ball from the server's side.
 *
 *   w             Pointer to the live PhysicsWorld.
 *   towardPlayer  If > 0 the ball is served toward the human player (AI
 *                 serves); if <= 0 the ball is served toward the AI (player
 *                 serves).
 *
 * The serve origin is scaled by tableL so it remains correct if the table
 * dimensions change.  A small random lateral offset is added to the initial
 * velocity for variety.
 */
void PhysicsServe(PhysicsWorld *w, int towardPlayer);

/*
 * PhysicsUpdate — Advance the simulation by one time step.
 *
 *   w           Pointer to the live PhysicsWorld.
 *   dt          Elapsed time since last frame (seconds).  Should be ≤ 1/30 s
 *               for numerical stability; the renderer typically supplies 1/60 s.
 *   input       Player control input for this frame (see PhysicsInput).
 *   difficulty  AI skill level in [0, 1].  0 = easy (large positional error,
 *               slow movement), 1 = hard (minimal error, fast movement).
 *   playerScore Pointer to the human player's score accumulator; incremented
 *               when the AI fails to return the ball.
 *   aiScore     Pointer to the AI score accumulator; incremented when the
 *               human player fails to return the ball.
 *   rally       Pointer to the current rally length counter; incremented each
 *               time the ball bounces on the table surface.
 */
void PhysicsUpdate(PhysicsWorld *w,
                   float         dt,
                   PhysicsInput  input,
                   float         difficulty,
                   int          *playerScore,
                   int          *aiScore,
                   int          *rally);

/*
 * PhysicsPredictLanding — Forward-simulate the ball trajectory to find where
 * the ball will next touch the table surface.
 *
 *   w        Pointer to the current PhysicsWorld (read-only; the live state
 *            is not mutated — a local copy of the ball is used).
 *   maxTime  Maximum simulation time to look ahead (seconds).  If the ball
 *            does not reach the table within this window, the ball's projected
 *            position at t = maxTime is returned instead.
 *
 * Returns a Vector3 of the predicted landing point.  The y component is
 * clamped to tableY so the caller gets a clean surface contact point.
 *
 * The prediction uses simplified Euler integration (gravity only, no drag or
 * Magnus) at a 120 Hz sub-step for speed.  This is accurate enough for AI
 * targeting at typical shot speeds.
 */
Vector3 PhysicsPredictLanding(const PhysicsWorld *w, float maxTime);

#endif /* NTT_PHYSICS_H */
