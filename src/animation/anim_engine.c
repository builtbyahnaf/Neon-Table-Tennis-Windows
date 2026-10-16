/*
 * anim_engine.c  —  Animation & Particle-Effect Subsystem Implementation
 * ========================================================================
 * Implements the five animation effects declared in anim_engine.h:
 *   1. Particle bursts  (AnimBurst / AnimUpdate particle loop)
 *   2. Screen shake     (AnimUpdate shake decay / AnimHitJuice)
 *   3. Slow-motion      (AnimUpdate slowMo decay / AnimHitJuice)
 *   4. Camera kick      (AnimUpdate cameraKick decay / AnimHitJuice)
 *   5. UI pulse         (AnimUpdate uiPulse decay / AnimHitJuice)
 *
 * All particle storage is a preallocated flat array (pool allocator).
 * Live particles are kept contiguous via swap-and-pop O(1) removal so
 * the renderer can memcpy the entire live slice without branching.
 *
 * Build dependencies: raylib, math_3d, stdlib, string.
 */

#include "anim_engine.h"
#include "math_3d.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * AnimInit
 * =========================================================================
 * Pool-allocator strategy
 * -----------------------
 * We allocate `capacity` Particle structs in one contiguous calloc block.
 * Every subsequent spawn is O(1): either write to particles[count++] when
 * the pool is not yet full, or overwrite a random existing slot when it is.
 * There are zero per-frame heap calls after this initialisation.
 *
 * calloc is used (rather than malloc) so that every field starts at zero /
 * NULL — this means AnimEngine is safe to read even before the first burst.
 * ========================================================================= */
void AnimInit(AnimEngine *a, size_t capacity)
{
    /* Zero the whole struct first so every scalar field starts at 0. */
    memset(a, 0, sizeof(*a));

    a->capacity  = capacity;

    /* calloc zero-initialises each Particle so life == 0 (dead) by default,
     * preventing any stale data from being rendered before the first burst. */
    a->particles = (Particle *)calloc(capacity, sizeof(Particle));
}

/* =========================================================================
 * AnimFree
 * =========================================================================
 * Releases the particle pool and zeroes the struct so:
 *   • The dangling pointer is cleared (prevents use-after-free).
 *   • count and capacity are 0, making further AnimUpdate calls safe no-ops.
 * Calling AnimFree on an already-freed AnimEngine is harmless because
 * free(NULL) is a no-op and memset just re-zeroes an already-zero struct.
 * ========================================================================= */
void AnimFree(AnimEngine *a)
{
    free(a->particles);

    /* Zero the entire struct, including the now-dangling pointer. */
    memset(a, 0, sizeof(*a));
}

/* =========================================================================
 * AnimBurst
 * =========================================================================
 * Emits `amount` particles originating at world position `at`.
 *
 * Direction sampling — upper hemisphere
 * --------------------------------------
 * Each raw direction vector is drawn from the box [-1,1] × [-0.2,1] × [-1,1]
 * and then normalised to the unit sphere.  The Y lower-bound of -0.2 biases
 * directions slightly upward so sparks arc out and up rather than diving
 * immediately into the floor.  After normalisation the distribution is
 * non-uniform (denser near the poles) but visually indistinguishable from
 * a uniform hemisphere at real-time frame rates.
 *
 * Speed scaling
 * -------------
 *   speed = impulse * (0.4 + rand[0,1])
 *
 * The [0.4, 1.4] multiplier gives each particle a randomised speed so the
 * burst fans out rather than forming a thin shell.
 *
 * Pool-full eviction
 * ------------------
 * When count == capacity we cannot grow the array, so we pick a random
 * existing slot to overwrite.  This keeps the most-recent burst fully
 * visible while gracefully retiring older particles.
 *
 * Bug fix (maxLife): the original code hardcoded maxLife = 1.35f for all
 * particles while life was randomised as 0.35 + rand[0,1].  The fade
 * ratio life/maxLife would therefore start below 1.0 and could go negative
 * for short-lived particles, producing incorrect alpha.  Now maxLife is set
 * to the same value as life at spawn, so the fade ratio starts at exactly
 * 1.0 and reaches 0.0 precisely when the particle expires.
 *
 * Bug fix (capacity == 0 guard): GetRandomValue(0, -1) is undefined
 * behaviour, so we return early when the pool has no slots.
 * ========================================================================= */
void AnimBurst(AnimEngine *a, Vector3 at, Color c, int amount, float impulse)
{
    /* Safety: if the engine was never initialised or has zero capacity,
     * calling GetRandomValue(0, capacity-1) would be UB (wrap to SIZE_MAX). */
    if (!a->particles || a->capacity == 0)
    {
        return;
    }

    for (int n = 0; n < amount; n++)
    {
        /* ---- Choose a destination slot ---------------------------------- */
        size_t i;
        if (a->count < a->capacity)
        {
            /* Pool has free space: claim the next sequential slot. O(1). */
            i = a->count++;
        }
        else
        {
            /* Pool is full: evict a random existing particle so the burst
             * still looks complete rather than silently dropping sparks. */
            i = (size_t)GetRandomValue(0, (int)a->capacity - 1);
        }

        /* ---- Sample a direction from the upper hemisphere --------------- */
        /*
         * Raw components are integers in the ranges shown, divided to get
         * floats.  Y is biased toward positive (upper hemisphere) so sparks
         * are launched upward and outward, not into the table surface.
         *
         *   raw.x in [-1.00,  1.00]
         *   raw.y in [-0.20,  1.00]   ← slight upward bias
         *   raw.z in [-1.00,  1.00]
         */
        Vector3 rawDir = {
            (float)GetRandomValue(-100, 100) / 100.0f,  /* X: full lateral spread  */
            (float)GetRandomValue(-20,  100) / 100.0f,  /* Y: biased upward        */
            (float)GetRandomValue(-100, 100) / 100.0f   /* Z: full depth spread    */
        };

        /* Normalise to unit length so speed is purely controlled by impulse.
         * V3Norm handles the near-zero-vector edge case internally. */
        Vector3 dir = V3Norm(rawDir);

        /* ---- Randomise speed within [0.4, 1.4] × impulse --------------- */
        /*
         * speed = impulse * (0.4 + rand[0,1])
         *
         * The 0.4 floor ensures no particle is stationary; the +rand[0,1]
         * spreads the burst into a volume rather than a thin shell.
         */
        float speed = impulse * (0.4f + (float)GetRandomValue(0, 100) / 100.0f);

        /* ---- Randomise lifetime ----------------------------------------- */
        /*
         * life = 0.35 + rand[0,1]  →  range [0.35, 1.35] seconds.
         *
         * BUG FIX: maxLife is set to the same value as life so the fade
         * ratio  (life / maxLife)  starts at 1.0 and ends at 0.0, giving
         * a correct linear alpha fade over the particle's full lifespan.
         * Previously maxLife was hardcoded to 1.35f which caused short-
         * lived particles to start already partially faded and could yield
         * a negative ratio near expiry.
         */
        float initialLife = 0.35f + (float)GetRandomValue(0, 100) / 100.0f;

        /* ---- Randomise visual size --------------------------------------- */
        /*
         * size = 0.025 + rand[0,100]/3000  →  range [0.025, 0.058] metres.
         * Dividing by 3000 keeps the variance small so all sparks look
         * similar in scale but have a little natural variation.
         */
        float size = 0.025f + (float)GetRandomValue(0, 100) / 3000.0f;

        /* ---- Write the initialised particle into the chosen slot -------- */
        a->particles[i] = (Particle){
            .pos     = at,
            .vel     = V3Scale(dir, speed),
            .life    = initialLife,
            .maxLife = initialLife,  /* fade ratio starts at 1.0 exactly */
            .size    = size,
            .color   = c
        };
    }
}

/* =========================================================================
 * AnimHitJuice
 * =========================================================================
 * Triggers the full suite of "game-feel" effects on a ball hit.
 * Two intensity presets are available, selected by the `smash` flag.
 *
 * Effect breakdown
 * ----------------
 * shake      — magnitude used by the renderer to randomly offset the camera
 *              each frame.  Decays linearly in AnimUpdate (rate 1.8 /s).
 *
 * slowMo     — remaining seconds of slow-motion.  The game loop should
 *              multiply `dt` by (1 - slowMo) to dilate time.  Decays
 *              linearly at 1.0 /s (i.e. the value is directly in seconds).
 *
 * cameraKick — world-space impulse added to the camera target.  Positive Y
 *              lifts the viewpoint slightly; positive Z pushes it toward the
 *              viewer (screen-space depth).  The vector is accumulated (+=)
 *              so rapid hits compound naturally before decaying.  Decays
 *              exponentially in AnimUpdate.
 *
 * uiPulse    — set to 1.0 so HUD elements flash at full intensity on every
 *              hit, regardless of smash / normal.  Decays at 2.5 /s.
 *
 *                           | smash   | normal |
 *              -------------|---------|--------|
 *              shake        | 0.34    | 0.10   |
 *              slowMo       | 0.18 s  | 0 s    |
 *              cameraKick.y | +0.10   | +0.025 |
 *              cameraKick.z | +0.20   | +0.06  |
 *              uiPulse      | 1.0     | 1.0    |
 * ========================================================================= */
void AnimHitJuice(AnimEngine *a, bool smash)
{
    /* Screen-shake magnitude; larger for a smash. */
    a->shake = smash ? 0.34f : 0.10f;

    /* Slow-motion duration (seconds); only triggered on a smash. */
    a->slowMo = smash ? 0.18f : 0.0f;

    /* Camera kick: accumulated so back-to-back hits compound.
     * +Y tilts the view up slightly; +Z pushes toward the viewer.
     * Positive Z is "toward the screen" in the game's coordinate system. */
    a->cameraKick = V3Add(
        a->cameraKick,
        (Vector3){
            0.0f,
            smash ? 0.10f  : 0.025f,  /* vertical kick */
            smash ? 0.20f  : 0.06f    /* depth kick toward camera */
        }
    );

    /* UI pulse always fires at full intensity on any hit. */
    a->uiPulse = 1.0f;
}

/* =========================================================================
 * AnimUpdate
 * =========================================================================
 * Advances all animation state by one frame.  Should be called with the
 * real (non-slowed) delta-time so effect durations are wall-clock accurate.
 *
 * Particle gravity
 * ----------------
 * Each frame the vertical velocity component is reduced by:
 *
 *   vel.y -= GRAVITY * dt       where GRAVITY = 1.2 m/s²
 *
 * 1.2 m/s² is roughly 1/8 of Earth gravity, chosen so sparks arc gently
 * and stay visible for their full lifetime rather than crashing to the
 * floor immediately.  The value is intentionally low because the game
 * world scale is also reduced (the table is a few metres long, not 2.74 m).
 *
 * Swap-and-pop O(1) removal
 * -------------------------
 * Keeping the live particle array contiguous without O(n) shifts:
 *   1. When particles[i].life <= 0, copy the last live particle into slot i.
 *   2. Decrement count.
 *   3. Do NOT increment i so the newly placed particle is tested next.
 * This is the classic "swap-and-pop" idiom.  Particle render order is
 * unimportant (all are additive-blended sparks) so reordering is fine.
 *
 * Exponential camera-kick decay
 * ------------------------------
 * Each frame:
 *
 *   cameraKick *= max(0, 1 - dt * 5)
 *
 * This is a first-order discrete exponential decay with rate constant 5 /s.
 * Continuous equivalent: cameraKick(t) = cameraKick₀ · e^(−5t).
 * The fmaxf(0, …) clamp prevents the factor from going negative when
 * dt > 0.2 s (e.g. after a long GC pause), which would otherwise cause
 * the kick to oscillate around zero instead of decaying smoothly.
 * ========================================================================= */
void AnimUpdate(AnimEngine *a, float dt)
{
    /* Advance wall-clock time (used for procedural animation in shaders). */
    a->time += dt;

    /* ---- Decay scalar effect states ------------------------------------ */

    /* Screen shake decays linearly at 1.8 units/second, floored at 0. */
    a->shake = fmaxf(0.0f, a->shake - dt * 1.8f);

    /* Slow-motion countdown: decrement by real dt, floored at 0. */
    a->slowMo = fmaxf(0.0f, a->slowMo - dt);

    /* UI pulse decays linearly at 2.5 units/second, floored at 0. */
    a->uiPulse = fmaxf(0.0f, a->uiPulse - dt * 2.5f);

    /* Camera-kick exponential decay.
     * Factor = max(0, 1 - dt*5).  Clamp prevents oscillation when dt > 0.2.
     * Continuous equivalent: kick(t) = kick₀ · e^(−5t). */
    float kickDecay = fmaxf(0.0f, 1.0f - dt * 5.0f);
    a->cameraKick = V3Scale(a->cameraKick, kickDecay);

    /* ---- Particle simulation ------------------------------------------- */
    for (size_t i = 0; i < a->count; /* i incremented conditionally below */)
    {
        Particle *p = &a->particles[i];

        /* Age the particle. */
        p->life -= dt;

        if (p->life <= 0.0f)
        {
            /* ---- Swap-and-pop O(1) removal ------------------------------ */
            /* Overwrite this dead particle with the last live one, then
             * shrink the live count.  Do not advance i so the replacement
             * particle is evaluated on the very next iteration. */
            a->particles[i] = a->particles[--a->count];
            continue; /* re-test slot i with the new occupant */
        }

        /* ---- Apply gravity ---------------------------------------------- */
        /*
         * Discrete Euler integration of gravitational acceleration:
         *   vel.y(t+dt) = vel.y(t) - GRAVITY * dt
         *
         * GRAVITY = 1.2 m/s²  (≈ 1/8 g, tuned for game-world scale).
         * Only the Y component is affected; X and Z coast at constant speed.
         */
        p->vel.y -= 1.2f * dt;

        /* ---- Integrate position ---------------------------------------- */
        /*
         * Euler position step:
         *   pos(t+dt) = pos(t) + vel(t+dt) * dt
         * Using the already-updated vel is semi-implicit Euler, which damps
         * rather than amplifies energy — suitable for short-lived sparks.
         */
        p->pos = V3Add(p->pos, V3Scale(p->vel, dt));

        i++; /* advance only when we did NOT remove the particle */
    }
}
