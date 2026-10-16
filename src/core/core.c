/*
 * =============================================================================
 * core.c  —  Neon Table Tennis  |  Application Core & Window Management
 * =============================================================================
 *
 * IMPLEMENTATION DETAILS
 * ----------------------
 * Implements window initialization, dynamic resolution tracking, frame boundary
 * hooks, and the finite state machine transitions for the top-level application.
 *
 * ARCHITECTURAL DESIGN
 * --------------------
 * 1. Resizable Window & MSAA:
 *    Configures Raylib with FLAG_WINDOW_RESIZABLE and FLAG_MSAA_4X_HINT before
 *    InitWindow() is called. This ensures sharp antialiased neon vector lines
 *    regardless of the display DPI or user window resizing.
 *
 * 2. Target FPS & Fixed Timestep Decoupling:
 *    The window is set to target 144 FPS (or display refresh rate), while the
 *    physics update runs deterministically inside a sub-step accumulator at 60 Hz.
 *
 * 3. State History:
 *    `previousState` tracks the immediate prior state, allowing modal screens
 *    (such as Pause or Options) to return cleanly to their originator.
 *
 * =============================================================================
 */

#include "core.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Core Lifecycle & Frame Hooks
 * --------------------------------------------------------------------------- */

/*
 * CoreInit
 * --------
 * Initializes the AppCore structure and creates the OS window.
 *
 * @param c  Pointer to AppCore structure to initialize.
 */
void CoreInit(AppCore *c)
{
    if (c == NULL) {
        return;
    }

    /* Initialize core structure defaults */
    *c = (AppCore){
        .state         = STATE_MENU,
        .previousState = STATE_MENU,
        .accumulator   = 0.0,
        .running       = true,
        .screenW       = NTT_SCREEN_W,
        .screenH       = NTT_SCREEN_H
    };

    /* Enable 4x Multi-Sample Anti-Aliasing for crisp neon lines, and resizable window */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);

    /* Create the OS application window */
    InitWindow(c->screenW, c->screenH, NTT_TITLE);

    /* Target high refresh rates (144 Hz) for ultra-smooth rendering */
    SetTargetFPS(144);
}

/*
 * CoreBeginFrame
 * --------------
 * Queries current display dimensions at the start of every render tick.
 * Handles user window resizing and DPI scaling seamlessly.
 *
 * @param c  Pointer to AppCore structure.
 */
void CoreBeginFrame(AppCore *c)
{
    if (c == NULL) {
        return;
    }

    c->screenW = GetScreenWidth();
    c->screenH = GetScreenHeight();
}

/*
 * CoreEndFrame
 * ------------
 * Checks OS window close events (e.g. clicking the 'X' button or Alt+F4)
 * or explicit requests to quit the application.
 *
 * @param c  Pointer to AppCore structure.
 */
void CoreEndFrame(AppCore *c)
{
    if (c == NULL) {
        return;
    }

    if (WindowShouldClose() || c->state == STATE_QUIT) {
        c->running = false;
    }
}

/* ---------------------------------------------------------------------------
 * State Machine Transitions
 * --------------------------------------------------------------------------- */

/*
 * CoreSetState
 * ------------
 * Transitions the global game state and records the previous state in history.
 *
 * @param c  Pointer to AppCore structure.
 * @param s  New GameState to transition into.
 */
void CoreSetState(AppCore *c, GameState s)
{
    if (c == NULL) {
        return;
    }

    c->previousState = c->state;
    c->state = s;
}
