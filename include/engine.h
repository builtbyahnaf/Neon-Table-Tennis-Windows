/*
 * =============================================================================
 * engine.h  —  Neon Table Tennis  |  Top-Level Game Engine
 * =============================================================================
 *
 * PURPOSE
 * -------
 * Declares the central GameEngine aggregate and the four lifecycle functions
 * that drive the entire game loop.  All subsystems (physics, animation, audio,
 * database, core window/state) are owned by a single GameEngine instance that
 * is stack-allocated in main().
 *
 * ARCHITECTURE
 * ------------
 * The engine enforces a strict two-phase tick:
 *
 *   EngineUpdate(g, dt)  — read input, advance simulation, mutate state.
 *                          NO rendering or drawing of any kind.
 *
 *   EngineDraw(g)        — read (not write) state, render everything to screen.
 *                          NO simulation or input consumption.
 *
 * This separation prevents the bug where UIButtonDraw() was called inside
 * EngineUpdate(), interleaving rendering with logic.
 *
 * GAME-STATE MACHINE
 * ------------------
 *   STATE_MENU  ──► STATE_MATCH       (Start Match button)
 *              ──► STATE_TUTORIAL     (How To Play button)
 *              ──► STATE_LEADERBOARD  (Leaderboard button)
 *              ──► (running=false)    (Quit button / ESC)
 *
 *   STATE_MATCH ──► STATE_PAUSE       (ESC key)
 *               ──► STATE_GAME_OVER   (first to 7 points)
 *
 *   STATE_PAUSE ──► STATE_MATCH       (Resume / ESC)
 *               ──► STATE_MENU        (Main Menu button)
 *
 *   STATE_TUTORIAL     ──► STATE_MENU (ENTER or click)
 *   STATE_LEADERBOARD  ──► STATE_MENU (ENTER or click)
 *   STATE_GAME_OVER    ──► STATE_MENU (ENTER or click)
 *
 * DEPENDENCIES
 * ------------
 *   raylib     — window, input, 3-D rendering (via core.h / physics.h)
 *   core.h     — AppCore (window, GameState machine)
 *   physics.h  — PhysicsWorld, PhysicsInput, PhysicsUpdate
 *   anim_engine.h — AnimEngine (particles, camera-kick, screen-shake)
 *   db_handler.h  — SQLite high-score persistence
 *   audio_mgr.h   — Sound effect playback
 *
 * =============================================================================
 */

#ifndef NTT_ENGINE_H
#define NTT_ENGINE_H

#include "core.h"
#include "physics.h"
#include "anim_engine.h"
#include "db_handler.h"
#include "audio_mgr.h"

/* ---------------------------------------------------------------------------
 * GameEngine
 * ---------------------------------------------------------------------------
 * Single owner of every subsystem.  Stack-allocate one instance in main()
 * and pass its address to every engine function.
 * --------------------------------------------------------------------------- */
typedef struct GameEngine {

    /* ── Subsystems ──────────────────────────────────────────────────────── */
    AppCore      core;      /* Window, GameState machine, fixed-step accumulator */
    PhysicsWorld physics;   /* Ball, paddles, table geometry, simulation state   */
    AnimEngine   anim;      /* Particle pool, screen-shake, camera-kick, UI pulse */
    DbHandler    db;        /* SQLite high-score database handle                  */
    AudioMgr     audio;     /* Loaded sound effects and master volume             */

    /* ── Match bookkeeping ───────────────────────────────────────────────── */
    int   playerScore;  /* Current player points (0–7).  First to 7 wins.       */
    int   aiScore;      /* Current AI points (0–7).                             */
    int   rally;        /* Number of consecutive ball touches this exchange      */
    int   wins;         /* Career wins (informational; persisted via db)         */
    int   losses;       /* Career losses (informational; persisted via db)       */

    /* ── Smash-charge state ──────────────────────────────────────────────── */
    float charge;       /* [0, 1] — how full the smash charge bar is            */
    bool  charging;     /* true while right mouse button is held down           */

    /* ── Per-match timing & AI difficulty ───────────────────────────────── */
    float matchTime;    /* Accumulated match time in seconds since StartMatch()  */
    float difficulty;   /* AI reaction scalar, ramps from 0.58 → 0.92 over time */

    /* ── End-of-match result ────────────────────────────────────────────── */
    bool matchWon;      /* true if player reached 7 first, false if AI did      */

    /* ── Rendering ──────────────────────────────────────────────────────── */
    Camera3D camera;    /* Base 3-D perspective camera (shake applied per frame) */

} GameEngine;

/* ---------------------------------------------------------------------------
 * EngineInit
 * ---------------------------------------------------------------------------
 * Initialises every subsystem and opens the game window.
 *
 * Parameters:
 *   g — pointer to a zero-or-garbage GameEngine to initialise.
 *
 * Returns:
 *   true on success.  Currently always succeeds; the database is optional
 *   (a warning is logged and the game runs without persistence if unavailable).
 * --------------------------------------------------------------------------- */
bool EngineInit(GameEngine *g);

/* ---------------------------------------------------------------------------
 * EngineShutdown
 * ---------------------------------------------------------------------------
 * Frees all subsystem resources and closes the window.
 * Must be called exactly once before the process exits.
 *
 * Parameters:
 *   g — pointer to the live GameEngine.
 * --------------------------------------------------------------------------- */
void EngineShutdown(GameEngine *g);

/* ---------------------------------------------------------------------------
 * EngineUpdate
 * ---------------------------------------------------------------------------
 * Reads player input and advances all game-logic for one fixed time-step.
 *
 * CONTRACT: This function must NEVER call any drawing function.
 *           All rendering is deferred to EngineDraw().
 *
 * Parameters:
 *   g  — pointer to the live GameEngine.
 *   dt — fixed physics time-step in seconds (typically NTT_FIXED_DT = 1/60 s).
 * --------------------------------------------------------------------------- */
void EngineUpdate(GameEngine *g, float dt);

/* ---------------------------------------------------------------------------
 * EngineDraw
 * ---------------------------------------------------------------------------
 * Renders the current game state to the back-buffer.
 *
 * CONTRACT: This function must NEVER mutate game state or consume input
 *           events.  It only reads from *g.
 *
 * Parameters:
 *   g — pointer to the live GameEngine (treated as logically const).
 * --------------------------------------------------------------------------- */
void EngineDraw(GameEngine *g);

#endif /* NTT_ENGINE_H */
