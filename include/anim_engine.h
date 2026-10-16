/*
 * anim_engine.h  —  Animation & Particle-Effect Subsystem
 * =========================================================
 * This module owns every transient visual effect in Neon Table Tennis:
 *
 *   • Particle bursts  – pools of short-lived glowing sparks that fly
 *     outward from a point, arc under gravity, and fade to transparent.
 *
 *   • Screen shake     – a decaying magnitude value that the renderer
 *     samples each frame to offset the camera position by a random
 *     vector scaled by `shake`.
 *
 *   • Slow-motion      – a decaying scalar in [0, 1] that the game loop
 *     multiplies onto `dt` so time appears to dilate after a smash.
 *
 *   • Camera kick      – a world-space impulse vector added to the camera
 *     target each frame that makes the viewpoint lurch then spring back
 *     via exponential decay.
 *
 *   • UI pulse         – a decaying scalar in [0, 1] that HUD elements
 *     sample to briefly inflate or brighten on a hit.
 *
 * Design: all particles live in a single flat array (preallocated on
 * AnimInit) so every allocation is O(1) and there is no per-frame heap
 * traffic.  When the pool is full, a new particle evicts a random
 * existing one so the most recent burst always looks complete.
 *
 * Dependencies: raylib (Vector3, Color, GetRandomValue), math_3d (V3*).
 */

#ifndef NTT_ANIM_ENGINE_H
#define NTT_ANIM_ENGINE_H

#include "raylib.h"
#include <stdbool.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Particle
 * A single spark emitted by AnimBurst.  Particles are value types stored
 * directly inside AnimEngine::particles[] — no heap indirection.
 * ----------------------------------------------------------------------- */
typedef struct Particle
{
    Vector3 pos;     /* World-space position (metres)                       */
    Vector3 vel;     /* World-space velocity (metres per second)            */
    float   life;    /* Remaining lifetime (seconds); particle dies at <= 0 */
    float   maxLife; /* Total lifetime at spawn (seconds); used for alpha   *
                      * fade: alpha = life / maxLife goes 1.0 → 0.0         */
    float   size;    /* Render radius (metres)                              */
    Color   color;   /* Base tint; renderer may modulate alpha with fade    */
} Particle;

/* -----------------------------------------------------------------------
 * AnimEngine
 * Central state bag for all animation effects.  Owns the particle pool.
 * Passed by pointer everywhere; callers allocate one on the stack or as a
 * game-state field and call AnimInit / AnimFree around it.
 * ----------------------------------------------------------------------- */
typedef struct AnimEngine
{
    /* --- Particle pool ------------------------------------------------- */
    Particle *particles; /* Preallocated flat array of Particle structs      */
    size_t    count;     /* Number of live particles currently in the array  */
    size_t    capacity;  /* Total slots in the array (fixed after AnimInit)  */

    /* --- Scalar effect states ------------------------------------------ */
    float shake;         /* Screen-shake magnitude, decays to 0 over time   */
    float slowMo;        /* Slow-motion remaining (seconds), decays to 0    */
    float uiPulse;       /* HUD pulse intensity in [0,1], decays to 0       */
    float time;          /* Monotonically increasing wall-clock (seconds)   */

    /* --- Camera effects ------------------------------------------------- */
    Vector3 cameraKick;  /* World-space offset applied to camera target;    *
                          * decays exponentially toward (0,0,0) each frame  */
} AnimEngine;

/* -----------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------- */

/*
 * AnimInit — allocate the particle pool and zero all state.
 *
 * @param a        Pointer to an uninitialised AnimEngine.
 * @param capacity Maximum number of simultaneous live particles.
 *                 Choosing a power-of-two (e.g. 512) has no algorithmic
 *                 benefit here but is conventional.
 */
void AnimInit(AnimEngine *a, size_t capacity);

/*
 * AnimFree — release the particle pool and zero all state.
 *
 * @param a  Pointer to a previously initialised AnimEngine.
 *           Safe to call multiple times (subsequent calls are no-ops
 *           because AnimFree zeroes the struct including the pointer).
 */
void AnimFree(AnimEngine *a);

/*
 * AnimUpdate — advance all animation state by one frame.
 *
 * Must be called once per game-loop iteration with the real (unscaled)
 * delta-time.  Moves particles, applies gravity, removes dead particles,
 * and decays all scalar effect states.
 *
 * @param a   Pointer to the AnimEngine.
 * @param dt  Elapsed seconds since last frame (real time, not slowed).
 */
void AnimUpdate(AnimEngine *a, float dt);

/*
 * AnimBurst — spawn a spray of particles at a world position.
 *
 * Directions are sampled from an upper hemisphere so sparks always fly
 * upward and outward, never straight down.  If the pool is full, each
 * new particle evicts a random existing one to keep the most recent
 * burst visually complete.
 *
 * @param a       Pointer to the AnimEngine.
 * @param at      World-space origin of the burst.
 * @param c       Base colour applied to every spawned particle.
 * @param amount  Number of particles to emit.
 * @param impulse Peak speed (m/s) before per-particle random scaling.
 */
void AnimBurst(AnimEngine *a, Vector3 at, Color c, int amount, float impulse);

/*
 * AnimHitJuice — trigger the suite of feel effects on a ball hit.
 *
 * Sets shake, slowMo, cameraKick, and uiPulse to values tuned for either
 * a normal hit or a power smash.
 *
 * @param a      Pointer to the AnimEngine.
 * @param smash  true → power-smash magnitudes; false → light-hit magnitudes.
 */
void AnimHitJuice(AnimEngine *a, bool smash);

#endif /* NTT_ANIM_ENGINE_H */
