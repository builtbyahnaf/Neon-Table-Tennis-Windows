/*
 * =============================================================================
 * core.h  --  Application Core: Windowing, Timing & Game-State Machine
 * =============================================================================
 *
 * PURPOSE
 *   Defines the top-level application skeleton for Neon Table Tennis.  It owns:
 *
 *     1. SCREEN / WINDOW constants  -- target resolution and title string.
 *     2. TIMING constants           -- the fixed-timestep physics parameters.
 *     3. GameState enum             -- every screen / mode the game can be in.
 *     4. AppCore struct             -- the single runtime context passed through
 *                                     every system (no global state).
 *     5. Core API declarations      -- lifecycle functions implemented in core.c.
 *
 * FIXED-TIMESTEP DESIGN
 *   The game uses a "fix your timestep" integration loop:
 *
 *     accumulator += rawDeltaTime (capped at NTT_MAX_FRAME_DT)
 *     while (accumulator >= NTT_FIXED_DT)
 *     {
 *         PhysicsTick(NTT_FIXED_DT);
 *         accumulator -= NTT_FIXED_DT;
 *     }
 *     Render(accumulator / NTT_FIXED_DT);   // interpolation alpha
 *
 *   This decouples physics accuracy from frame rate: slow frames do multiple
 *   ticks; fast frames may skip a tick entirely.  NTT_MAX_FRAME_DT prevents
 *   the "spiral of death" where a very slow frame causes so many catch-up
 *   ticks that subsequent frames are also slow.
 *
 * DEPENDENCIES
 *   raylib.h   -- window management, rendering context
 *   stdbool.h  -- bool type
 *
 * =============================================================================
 */

#ifndef NTT_CORE_H
#define NTT_CORE_H

#include "raylib.h"    /* Window / rendering context                          */
#include <stdbool.h>   /* bool, true, false                                   */


/* =============================================================================
 * Section 1 -- Window & Identity Constants
 * =============================================================================
 */

/* Target horizontal resolution in pixels. The window is created at this width;
 * all UI layout constants should be derived from this value. */
#define NTT_SCREEN_W        1440

/* Target vertical resolution in pixels. */
#define NTT_SCREEN_H        900

/* Human-readable title shown in the OS window title bar. */
#define NTT_TITLE           "NEON PONG // 3D TABLE TENNIS"

/* Short semantic version string used on the title screen and in log output.
 * Format: MAJOR.MINOR.PATCH following Semantic Versioning 2.0.0. */
#define NTT_TITLE_VERSION   "0.1.0"


/* =============================================================================
 * Section 2 -- Fixed-Timestep Physics Parameters
 * =============================================================================
 */

/* Duration of one physics integration step, in seconds.
 * 1/60 s keeps the simulation in sync with a 60 Hz display and gives
 * fine-grained ball/paddle collision detection. */
#define NTT_FIXED_DT        (1.0 / 60.0)

/* Maximum raw delta-time accepted before clamping, in seconds.
 * If a frame takes longer than this (e.g., debugger breakpoint, OS jitter),
 * the accumulator is capped here to avoid running hundreds of physics ticks
 * in a single frame (the "spiral of death"). 0.25 s == 4 missed frames. */
#define NTT_MAX_FRAME_DT    0.25


/* =============================================================================
 * Section 3 -- Game State Machine
 * =============================================================================
 *
 * Every screen / mode of the application maps to exactly one GameState value.
 * State transitions are performed exclusively through CoreSetState() so that
 * entry/exit side-effects (e.g., music crossfade, resource load) are centralised.
 *
 * Transition diagram (simplified):
 *
 *   MENU --> TUTORIAL --> MATCH <--> PAUSE
 *             |              |
 *             v              v
 *          MATCH         GAME_OVER --> LEADERBOARD --> MENU
 *                                            |
 *                                            v
 *                                          QUIT
 */
typedef enum GameState
{
    STATE_MENU,          /* Main menu: play, options, quit                    */
    STATE_TUTORIAL,      /* Interactive tutorial teaching controls/rules      */
    STATE_MATCH,         /* Active gameplay: ball in play, scoring live       */
    STATE_PAUSE,         /* Game paused mid-match; overlay shown over MATCH   */
    STATE_LEADERBOARD,   /* High-score / results table after a match ends     */
    STATE_GAME_OVER,     /* Post-match summary screen (winner announcement)   */
    STATE_QUIT           /* Sentinel: main loop exits when this state is set  */
} GameState;


/* =============================================================================
 * Section 4 -- Application Core Context
 * =============================================================================
 *
 * AppCore is the single authoritative runtime context for the application.
 * It is allocated on the stack in main() and passed by pointer to every
 * system. There is intentionally no global instance.
 */
typedef struct AppCore
{
    GameState state;          /* Current active game state                    */
    GameState previousState;  /* State prior to the last CoreSetState() call;
                               * used to restore state after pausing          */

    double    accumulator;    /* Leftover time (seconds) carried forward into
                               * the next frame for the fixed-timestep loop   */

    bool      running;        /* false triggers a clean shutdown on next loop  */

    int       screenW;        /* Actual window width  (pixels); may differ from
                               * NTT_SCREEN_W if the OS enforces DPI scaling  */
    int       screenH;        /* Actual window height (pixels)                */
} AppCore;


/* =============================================================================
 * Section 5 -- Core API
 * =============================================================================
 */

/*
 * CoreInit -- Initialise the application core and open the OS window.
 *
 * Must be called once at program startup before any other system is used.
 * Sets `core->running = true`, `core->accumulator = 0`, opens the raylib
 * window at NTT_SCREEN_W x NTT_SCREEN_H, and enters STATE_MENU.
 *
 * Parameters:
 *   core  --  Pointer to a caller-allocated AppCore (need not be zeroed).
 */
void CoreInit(AppCore *core);

/*
 * CoreBeginFrame -- Advance the frame timer and compute the physics budget.
 *
 * Should be called once at the top of the main loop, after raylib's
 * BeginDrawing().  Reads the raw delta time from raylib, clamps it to
 * NTT_MAX_FRAME_DT, and adds the result to `core->accumulator`.
 *
 * Parameters:
 *   core  --  Pointer to the active AppCore.
 */
void CoreBeginFrame(AppCore *core);

/*
 * CoreEndFrame -- Finalise the current frame.
 *
 * Should be called once at the bottom of the main loop, before
 * raylib's EndDrawing().  Handles frame-rate limiter bookkeeping and
 * checks for the OS close-window event (sets `core->running = false`).
 *
 * Parameters:
 *   core  --  Pointer to the active AppCore.
 */
void CoreEndFrame(AppCore *core);

/*
 * CoreSetState -- Transition the game to a new state.
 *
 * Stores the current state in `core->previousState` before applying the
 * new one, enabling one-level undo (e.g., returning from PAUSE to MATCH).
 * Fires any entry/exit callbacks registered for the old and new states.
 *
 * Parameters:
 *   core   --  Pointer to the active AppCore.
 *   state  --  The GameState to transition into.
 */
void CoreSetState(AppCore *core, GameState state);


#endif /* NTT_CORE_H */
