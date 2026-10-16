/*
 * =============================================================================
 * main.c  —  Neon Table Tennis  |  Game Entry Point & Master Loop
 * =============================================================================
 *
 * ARCHITECTURAL DESIGN: FIXED TIMESTEP WITH VARIABLE RENDERING
 * -------------------------------------------------------------
 * This game loop implements the classic decoupled simulation pattern (commonly
 * referred to as Glenn Fiedler's "Fix Your Timestep!"):
 *
 *   1. Variable Frame Time:
 *      The renderer runs at whatever refresh rate the display and GPU support
 *      (e.g., 60 Hz, 144 Hz, 240 Hz).  `GetFrameTime()` provides the elapsed
 *      real time `frame` since the previous frame.
 *
 *   2. Clamped Delta Time:
 *      If the game window is moved, minimized, or hit by an OS hitch, `frame`
 *      could spike to multiple seconds. To prevent this, `frame` is clamped
 *      to `NTT_MAX_FRAME_DT` (0.25 s, or 4 missed frames at 60 Hz).
 *
 *   3. Sub-Step Accumulator:
 *      Elapsed time is added to `accumulator`. As long as `accumulator >= NTT_FIXED_DT`
 *      (1/60th of a second ≈ 0.016667 s), the engine advances physics by exactly
 *      `NTT_FIXED_DT`. This guarantees deterministic, stable, reproducible
 *      collision responses and flight trajectories regardless of frame rate.
 *
 *   4. Spiral-of-Death Protection:
 *      If the physics calculation takes longer than the fixed timestep, a slow
 *      machine would simulate more and more sub-steps each frame, causing the game
 *      to grind to a halt. A safety clamp limits updates to at most 8 sub-steps
 *      per render frame. If this threshold is hit, the accumulator is zeroed
 *      to shed accumulated debt immediately.
 *
 *   5. Decoupled Draw Phase:
 *      Once physics catch up, `EngineDraw()` renders the interpolated game scene
 *      once per frame, followed by `CoreEndFrame()` window polling.
 *
 * =============================================================================
 */

#include "engine.h"
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Application Entry Point
 * --------------------------------------------------------------------------- */
int main(void)
{
    /* 1. Allocate top-level game engine container on the stack */
    GameEngine game;

    /* 2. Initialize all subsystems (Core, Physics, Animation, Audio, Database) */
    if (!EngineInit(&game)) {
        fprintf(stderr, "[FATAL] Failed to initialize Neon Table Tennis engine.\n");
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Master Game Loop
     * ----------------------------------------------------------------------- */
    while (game.core.running) {

        /* 3. Begin frame: update resolution and input polling */
        CoreBeginFrame(&game.core);

        /* 4. Measure delta time and clamp against large hitches */
        double frameTime = (double)GetFrameTime();
        if (frameTime > NTT_MAX_FRAME_DT) {
            frameTime = NTT_MAX_FRAME_DT;
        }

        /* 5. Accumulate real time into the simulation budget */
        game.core.accumulator += frameTime;

        /* 6. Consume accumulated time in fixed 1/60s slices (up to 8 steps) */
        int steps = 0;
        while (game.core.accumulator >= NTT_FIXED_DT && steps < 8) {
            EngineUpdate(&game, (float)NTT_FIXED_DT);
            game.core.accumulator -= NTT_FIXED_DT;
            steps++;
        }

        /* If spiral-of-death cap was reached, drop leftover time debt */
        if (steps >= 8) {
            game.core.accumulator = 0.0;
        }

        /* 7. Render phase: clear screen, draw 3D scene & 2D HUD */
        BeginDrawing();
        EngineDraw(&game);
        EndDrawing();

        /* 8. End frame: poll OS events, handle window close or quit state */
        CoreEndFrame(&game.core);
    }

    /* -----------------------------------------------------------------------
     * Graceful Teardown
     * ----------------------------------------------------------------------- */
    EngineShutdown(&game);

    return 0;
}
