/*
 * =============================================================================
 * engine.c  —  Neon Table Tennis  |  Top-Level Game Engine Implementation
 * =============================================================================
 *
 * PURPOSE
 * -------
 * Implements the four GameEngine lifecycle functions declared in engine.h.
 * This translation unit is the integration layer between all subsystems:
 * physics, animation, audio, database, UI rendering, and the core state machine.
 *
 * TWO-PHASE DESIGN  (critical architecture rule)
 * -----------------------------------------------
 * EngineUpdate() — pure logic & input.  No draw calls, EVER.
 * EngineDraw()   — pure rendering.      No state mutation, EVER.
 *
 * Previously, UIButtonDraw() was called inside EngineUpdate().  UIButtonDraw()
 * renders to the screen AND returns a click result — mixing the logic and draw
 * phases.  The fix is to use CheckCollisionPointRec + IsMouseButtonReleased
 * directly in EngineUpdate() for click detection, and to call UIButtonDraw()
 * only inside EngineDraw().  This prevents:
 *   • Buttons being drawn twice in the pause state.
 *   • Draw calls occurring before BeginDrawing() or after EndDrawing().
 *   • Frame-timing artefacts from hover animations being advanced in Update.
 *
 * MENU BUTTON HIT-TESTING  (in EngineUpdate)
 * -------------------------------------------
 * Because UIButtonDraw() cannot be called in EngineUpdate(), button clicks are
 * detected manually:
 *
 *   Vector2 mouse = GetMousePosition();
 *   if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
 *       CheckCollisionPointRec(mouse, b.bounds)) { ... }
 *
 * IsMouseButtonReleased is preferred over IsMouseButtonPressed because it
 * matches human expectation (the action fires when you lift the finger) and
 * avoids accidental double-fires on high-frame-rate loops.
 *
 * =============================================================================
 */

#include "engine.h"
#include "ui_system.h"
#include "math_3d.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* =============================================================================
 * Module-level colour constants
 * =============================================================================
 * Defined once here so every draw call uses consistent palette entries.
 * All values are RGBA (red, green, blue, alpha).
 * ============================================================================= */
static const Color C_BG    = {  5,   9,  24, 255 }; /* Deep-space navy background        */
static const Color C_CYAN  = { 40, 235, 255, 255 }; /* Player paddle / ball tint         */
static const Color C_PINK  = {255,  55, 190, 255 }; /* AI paddle / smash burst           */
static const Color C_GREEN = { 75, 255, 180, 255 }; /* Ball and rally-bounce burst        */

/* =============================================================================
 * Static helper: MouseToPaddle
 * =============================================================================
 * Converts the current 2-D screen-space mouse position into a 3-D world-space
 * target position for the player's paddle.
 *
 * COORDINATE MAPPING
 * ------------------
 * Screen pixels → Normalised Device Coordinates (NDC) → World space:
 *
 *   nx  =  (mouse.x / screenW) * 2 - 1        range [-1, +1], left → right
 *   ny  =  (mouse.y / screenH) * 2 - 1        range [-1, +1], top  → bottom
 *
 * World X:
 *   target.x = nx * 2.1f
 *     Maps the full screen width to ±2.1 world units, matching the playable
 *     lateral extent of the player's half of the table (tableW ≈ 4.2 units).
 *
 * World Y (height):
 *   target.y = tableY + 0.95f - ny * 0.95f
 *     - tableY is the table surface height.
 *     - +0.95f offsets the paddle above the table surface.
 *     - -ny * 0.95f maps vertical mouse movement: moving the mouse up (ny < 0)
 *       raises the paddle; moving it down (ny > 0) lowers it.
 *
 * World Z (depth — player's near half):
 *   target.z = clamp( computed_z, tableL*0.05f, tableL*0.5f - 0.2f )
 *     - Fixed at 3.45f in NDC, which places the paddle at the near edge of the
 *       player's half.  The clamp ensures the paddle never crosses the net
 *       (tableL*0.5f - 0.2f) and never falls off the far end of the player's
 *       half (tableL*0.05f).
 *
 * Parameters:
 *   g — pointer to GameEngine; used to read physics.tableY and physics.tableL.
 *
 * Returns:
 *   A Vector3 in world space — the desired centre of the player paddle.
 * ============================================================================= */
static Vector3 MouseToPaddle(GameEngine *g)
{
    Vector2 m  = GetMousePosition();

    /* ── Normalised Device Coordinates ────────────────────────────────────── */
    float nx = (m.x / (float)GetScreenWidth())  * 2.0f - 1.0f; /* [-1, +1] L→R */
    float ny = (m.y / (float)GetScreenHeight()) * 2.0f - 1.0f; /* [-1, +1] T→B */

    /* ── World-space target ─────────────────────────────────────────────── */
    float wx = nx * 2.1f;                                   /* lateral   */
    float wy = g->physics.tableY + 0.40f - ny * 0.45f;     /* vertical: aligned with ball bounce height */

    /*
     * World Z: fixed paddle depth at 3.45f (player's near edge), then clamped
     * so the paddle stays strictly within the player's own half of the table:
     *   minimum  = tableL * 0.05f           (5 % from the near end)
     *   maximum  = tableL * 0.5f - 0.2f     (net minus a 0.2-unit safety gap)
     */
    float zMin = g->physics.tableL * 0.05f;
    float zMax = g->physics.tableL * 0.5f - 0.2f;
    float wz   = ClampF(3.45f, zMin, zMax);

    return (Vector3){ wx, wy, wz };
}

/* =============================================================================
 * Static helper: StartMatch
 * =============================================================================
 * Resets all per-match state and transitions the game into STATE_MATCH.
 *
 * Resets performed:
 *   • playerScore, aiScore, rally → 0
 *   • charge → 0, charging → false
 *   • matchTime → 0  (used by the difficulty ramp formula)
 *   • difficulty → 0.65f  (slightly above the ramp floor of 0.58f;
 *                           gives the AI a small head-start reaction)
 *   • PhysicsWorld is fully re-initialised so the ball is in a clean state.
 *
 * Parameters:
 *   g — pointer to the live GameEngine.
 * ============================================================================= */
static void StartMatch(GameEngine *g)
{
    g->playerScore = 0;
    g->aiScore     = 0;
    g->rally       = 0;
    g->charge      = 0.0f;
    g->charging    = false;
    g->matchTime   = 0.0f;
    g->difficulty  = 0.65f;   /* See difficulty ramp comment in EngineUpdate */

    PhysicsInit(&g->physics);
    CoreSetState(&g->core, STATE_MATCH);
}

/* =============================================================================
 * EngineInit
 * =============================================================================
 * Initialises every subsystem in dependency order and opens the game window.
 *
 * Initialisation order:
 *   1. memset to zero — guarantees no garbage in any field.
 *   2. CoreInit   — opens the OS window via raylib InitWindow().
 *   3. PhysicsInit — sets table dimensions, gravity, serve timer.
 *   4. AnimInit   — allocates particle pool (capacity 1200).
 *   5. DBOpen     — opens (or creates) the SQLite score database.
 *                   Failure is non-fatal; a warning is logged and the game
 *                   runs without persistence (db.available = false).
 *   6. AudioInit  — loads .wav/.ogg sound effects; requires the window to
 *                   exist (InitAudioDevice is called inside AudioInit).
 *   7. Camera3D   — fixed perspective camera aimed at the table centre.
 *
 * CAMERA SETUP
 * ------------
 *   position = {0, 7.5, 12.0}   — elevated behind the player's baseline.
 *   target   = {0, 1.0, 0}      — aimed at a point 1 unit above the table.
 *   up       = {0, 1, 0}        — standard world-up vector.
 *   fovy     = 48°              — moderately narrow FOV keeps table in frame
 *                                 without excessive perspective distortion.
 *   projection = CAMERA_PERSPECTIVE
 *
 * Parameters:
 *   g — pointer to an uninitialised (or zeroed) GameEngine.
 *
 * Returns:
 *   true — always succeeds (database failure is logged but non-fatal).
 * ============================================================================= */
bool EngineInit(GameEngine *g)
{
    /* Zero-init prevents any subsystem seeing garbage data during init. */
    memset(g, 0, sizeof(*g));

    CoreInit(&g->core);
    PhysicsInit(&g->physics);
    AnimInit(&g->anim, 1200);   /* 1 200 simultaneous particles maximum */

    if (!DBOpen(&g->db, "neon_table_tennis.sqlite")) {
        TraceLog(LOG_WARNING,
                 "High score database unavailable: %s", g->db.error);
        /* db.available = false; game continues without persistence */
    }

    AudioInit(&g->audio);

    /*
     * Base camera — screen-shake offsets are applied each frame in EngineDraw()
     * by adding g->anim.cameraKick and a sinusoidal jitter to a copy of this
     * camera before passing it to BeginMode3D().  The original is never mutated
     * so that shake decays cleanly without accumulating drift.
     */
    g->camera = (Camera3D){
        .position   = { 0.0f, 7.5f, 12.0f },
        .target     = { 0.0f, 1.0f,  0.0f },
        .up         = { 0.0f, 1.0f,  0.0f },
        .fovy       = 48.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    return true;
}

/* =============================================================================
 * EngineShutdown
 * =============================================================================
 * Releases subsystem resources in reverse initialisation order, then closes
 * the OS window.
 *
 * Order:
 *   1. AudioFree — unloads sounds, closes audio device.
 *   2. DBClose   — flushes WAL and closes the SQLite connection.
 *   3. AnimFree  — frees the particle pool heap allocation.
 *   4. CloseWindow — destroys the raylib window and OpenGL context.
 *      (PhysicsWorld and AppCore contain no heap allocations; no explicit
 *       free needed.)
 *
 * Parameters:
 *   g — pointer to the live GameEngine.
 * ============================================================================= */
void EngineShutdown(GameEngine *g)
{
    AudioFree(&g->audio);
    DBClose(&g->db);
    AnimFree(&g->anim);
    CloseWindow();
}

/* =============================================================================
 * EngineUpdate
 * =============================================================================
 * Advances the simulation by one fixed time-step dt and processes all player
 * input.  This function contains ZERO draw calls.
 *
 * STRUCTURE BY STATE
 * ------------------
 *
 * ALL STATES — global ESC handling:
 *   Match  → Pause
 *   Pause  → Match (resume)
 *   Others → Menu
 *   Menu   → quit (running = false)
 *
 * STATE_MENU / STATE_PAUSE — button click detection (no drawing):
 *   We cannot call UIButtonDraw() here because it renders to screen.
 *   Instead each button's Rectangle is defined, then:
 *     IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
 *     CheckCollisionPointRec(mouse, b.bounds)
 *   gives a clean, draw-free click test.  UIButtonDraw() is called later in
 *   EngineDraw() to animate the hover glow and handle the visual feedback.
 *
 * STATE_TUTORIAL / STATE_LEADERBOARD / STATE_GAME_OVER:
 *   Simple ENTER-or-click to return to menu.
 *
 * STATE_MATCH:
 *   1. Advance matchTime and recompute difficulty.
 *   2. Build PhysicsInput from mouse position and button states.
 *   3. Handle smash-charge (right mouse hold → release).
 *   4. Call PhysicsUpdate; detect score changes and rally bounces.
 *   5. Play audio and spawn particle bursts for events.
 *   6. Detect match end (first to 7) → persist score → STATE_GAME_OVER.
 *
 * DIFFICULTY RAMP FORMULA
 * -----------------------
 *   difficulty = clamp( 0.58 + matchTime / 150.0,  0.58,  0.92 )
 *
 *   At t=0 s  → difficulty = 0.58  (AI runs at 58 % reaction speed)
 *   At t=51 s → difficulty ≈ 0.92  (maximum — AI is nearly perfect)
 *   After t=51 the clamp holds it at 0.92 so the AI is never unbeatable.
 *   The ramp rate is 1/150 per second, meaning it takes 51 seconds of play
 *   time to go from floor to ceiling.
 *
 * SMASH CHARGE POWER FORMULA
 * --------------------------
 *   speed passed to AudioPlayHit = 7 + charge * 4
 *
 *   charge ∈ [0, 1].
 *   Minimum speed (no charge)     : 7 + 0*4 = 7
 *   Maximum speed (full charge)   : 7 + 1*4 = 11
 *   This gives the audio system a [7, 11] speed scalar to map to pitch/volume,
 *   making a charged smash sound noticeably harder than a regular drive.
 *
 * Parameters:
 *   g  — pointer to the live GameEngine.
 *   dt — fixed physics time-step in seconds.
 * ============================================================================= */
void EngineUpdate(GameEngine *g, float dt)
{
    AppCore *c = &g->core;

    /* Always tick the animation engine first so particle lifetimes and the
     * camera-kick decay are up to date regardless of which state we're in. */
    AnimUpdate(&g->anim, dt);

    /* ── Global ESC key handling ──────────────────────────────────────────── */
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (c->state == STATE_MATCH) {
            /* Pause the running match. */
            CoreSetState(c, STATE_PAUSE);
        } else if (c->state == STATE_PAUSE) {
            /* Resume from pause — ESC acts as a toggle. */
            CoreSetState(c, STATE_MATCH);
        } else if (c->state != STATE_MENU) {
            /* Any other non-menu screen: return to main menu. */
            CoreSetState(c, STATE_MENU);
        } else {
            /* Already at the main menu: quit the application. */
            c->running = false;
        }
    }

    /* ── STATE_MENU ──────────────────────────────────────────────────────── */
    if (c->state == STATE_MENU) {
        /*
         * Define the four menu button rectangles.  Layout is centred
         * horizontally, stacked vertically with 80px spacing.
         * Button click detection uses IsMouseButtonReleased + AABB test.
         * UIButtonDraw() is NOT called here — that happens in EngineDraw().
         */
        float cx = (float)(GetScreenWidth() / 2 - 140);
        Rectangle rStart       = { cx, 390.0f, 280.0f, 60.0f };
        Rectangle rHowToPlay   = { cx, 470.0f, 280.0f, 60.0f };
        Rectangle rLeaderboard = { cx, 550.0f, 280.0f, 60.0f };
        Rectangle rQuit        = { cx, 630.0f, 280.0f, 60.0f };

        Vector2 mouse    = GetMousePosition();
        bool    clicked  = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        if (clicked && CheckCollisionPointRec(mouse, rStart)) {
            AudioPlayUI(&g->audio);
            StartMatch(g);

        } else if (clicked && CheckCollisionPointRec(mouse, rHowToPlay)) {
            AudioPlayUI(&g->audio);
            CoreSetState(c, STATE_TUTORIAL);

        } else if (clicked && CheckCollisionPointRec(mouse, rLeaderboard)) {
            AudioPlayUI(&g->audio);
            CoreSetState(c, STATE_LEADERBOARD);

        } else if (clicked && CheckCollisionPointRec(mouse, rQuit)) {
            c->running = false;
        }

    /* ── STATE_TUTORIAL ──────────────────────────────────────────────────── */
    } else if (c->state == STATE_TUTORIAL) {
        /* Any confirmation input returns to the main menu. */
        if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            CoreSetState(c, STATE_MENU);
        }

    /* ── STATE_LEADERBOARD ───────────────────────────────────────────────── */
    } else if (c->state == STATE_LEADERBOARD) {
        if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            CoreSetState(c, STATE_MENU);
        }

    /* ── STATE_PAUSE ─────────────────────────────────────────────────────── */
    } else if (c->state == STATE_PAUSE) {
        /*
         * Two buttons: Resume and Main Menu.
         * Same hit-test pattern as STATE_MENU — no drawing here.
         * UIButtonDraw() for these buttons lives in EngineDraw().
         */
        float cx = (float)(GetScreenWidth() / 2 - 130);
        Rectangle rResume = { cx, 430.0f, 260.0f, 58.0f };
        Rectangle rMenu   = { cx, 510.0f, 260.0f, 58.0f };

        Vector2 mouse   = GetMousePosition();
        bool    clicked = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

        if (clicked && CheckCollisionPointRec(mouse, rResume)) {
            CoreSetState(c, STATE_MATCH);
        } else if (clicked && CheckCollisionPointRec(mouse, rMenu)) {
            CoreSetState(c, STATE_MENU);
        }

    /* ── STATE_MATCH ─────────────────────────────────────────────────────── */
    } else if (c->state == STATE_MATCH) {

        /* ── Step 1: Advance match clock and update AI difficulty ── */
        g->matchTime += dt;

        /*
         * Difficulty ramp (see formula in function block-comment above):
         *   difficulty = clamp( 0.58 + matchTime/150.0,  0.58,  0.92 )
         *
         * The value is passed to PhysicsUpdate, which uses it to scale the
         * AI paddle's reaction speed and prediction accuracy.
         */
        g->difficulty = ClampF(0.58f + g->matchTime / 150.0f, 0.58f, 0.92f);

        /* ── Step 2: Build physics input from mouse and keyboard ── */
        PhysicsInput in = {
            .target = MouseToPaddle(g),                         /* Desired paddle world pos */
            .hit    = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                      IsMouseButtonDown(MOUSE_BUTTON_LEFT),     /* Drive the ball (click or hold) */
            .smash  = false,                                     /* Overridden below         */
            .charge = 0.0f                                       /* Overridden below         */
        };

        /* ── Step 3: Smash-charge accumulation (right mouse button) ── */

        /* Right click start: begin charging. */
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            g->charging = true;
            g->charge   = 0.0f;
        }

        /* While held: accumulate charge at a rate of 0.65 per second,
         * clamped to [0, 1] so it never exceeds a full bar. */
        if (g->charging && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            g->charge = ClampF(g->charge + dt * 0.65f, 0.0f, 1.0f);
        }

        /* Right click release: fire the smash with the accumulated charge. */
        if (g->charging && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            in.hit    = true;
            in.smash  = true;
            in.charge = g->charge;  /* [0, 1] — passed to audio & physics */
            g->charging = false;

            /* Trigger a camera-kick and screen-shake via the anim engine. */
            AnimHitJuice(&g->anim, true);
        }

        /* ── Step 4: Physics simulation step ── */

        /* Snapshot scores and last hitter before the update to detect hit events. */
        int oldRally  = g->rally;
        int oldPS     = g->playerScore;
        int oldAS     = g->aiScore;
        int oldHitter = g->physics.lastHitter;

        PhysicsUpdate(
            &g->physics,
            dt,
            in,
            g->difficulty,
            &g->playerScore,
            &g->aiScore,
            &g->rally
        );

        /* ── Step 5: Audio and particle events triggered by physics ── */

        /* Paddle hit event: triggers whenever either player or AI strikes the ball. */
        if (g->physics.lastHitter != oldHitter && g->physics.ball.inPlay) {
            bool isPlayer = (g->physics.lastHitter == 1);
            bool isSmash  = isPlayer && in.smash;
            float hitSpeed = V3Len(g->physics.ball.velocity);

            AudioPlayHit(&g->audio, isSmash, hitSpeed);
            AnimHitJuice(&g->anim, isSmash);
            AnimBurst(
                &g->anim,
                g->physics.ball.position,
                isPlayer ? (isSmash ? C_PINK : C_CYAN) : C_PINK,
                isSmash ? 45 : 20,
                isSmash ? 3.5f : 1.8f
            );
        }

        /* Table-bounce event: rally counter incremented AND ball near surface. */
        if (g->rally > oldRally &&
            g->physics.ball.position.y <=
                g->physics.tableY + g->physics.ball.radius + 0.03f) {
            AudioPlayBounce(&g->audio);
            AnimBurst(&g->anim, g->physics.ball.position, C_GREEN, 8, 1.0f);
        }

        /* ── Step 6: Score-change detection and match-end logic ── */
        if (oldPS != g->playerScore || oldAS != g->aiScore) {

            /* Always spawn a dramatic burst at a fixed mid-table position
             * to signal that a point was scored. */
            AnimBurst(&g->anim, (Vector3){ 0.0f, 2.0f, 0.0f }, C_PINK, 45, 3.0f);

            /* First to 7 points wins the match. */
            if (g->playerScore >= 7 || g->aiScore >= 7) {
                g->matchWon = (g->playerScore > g->aiScore);

                /* Persist the result to the high-score database. */
                DBRecordMatch(&g->db, g->playerScore, g->rally, g->matchWon);

                CoreSetState(c, STATE_GAME_OVER);
            }
        }

    /* ── STATE_GAME_OVER ─────────────────────────────────────────────────── */
    } else if (c->state == STATE_GAME_OVER) {
        if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            CoreSetState(c, STATE_MENU);
        }
    }
}

/* =============================================================================
 * Static helper: DrawArena
 * =============================================================================
 * Renders all 3-D geometry for the match view: table, net, paddles, ball,
 * ball motion trail, and the active particle effects.
 *
 * This function is called between BeginMode3D() / EndMode3D() in EngineDraw()
 * and must not be called outside that block.
 *
 * GEOMETRY BREAKDOWN
 * ------------------
 *   Floor plane  — large dark quad (200×200 units) for the room floor.
 *   Table body   — blue cube: width=tableW, height=0.24, depth=tableL.
 *   Table surface— slightly smaller teal inset, sits flush on top.
 *   Net body     — translucent cube spanning the table width at Z=0.
 *   Net rail     — solid cyan bar across the top of the net.
 *   Grid lines   — 5 longitudinal faint lines for visual depth reference.
 *   Player paddle— cyan cube with white wire overlay.
 *   AI paddle    — pink cube with white wire overlay.
 *   Ball         — green sphere + wire mesh if inPlay; skipped if not.
 *   Ball trail   — 8 trailing ghost spheres fading in alpha and shrinking,
 *                  positioned along the ball's past velocity path:
 *                    tailPos[i] = ballPos - velocity * 0.018 * i
 *                    tailRadius = ballRadius * (1 - i/10)
 *                    tailAlpha  = 160 - i*16
 *   Particles    — each live particle drawn as a sphere; alpha and size both
 *                  scale with the normalised lifetime fraction f = life/maxLife.
 *
 * Parameters:
 *   g — pointer to the live GameEngine (read-only intent).
 * ============================================================================= */
static void DrawArena(GameEngine *g)
{
    PhysicsWorld *w = &g->physics;

    /* ── Floor ────────────────────────────────────────────────────────────── */
    DrawPlane(
        (Vector3){ 0.0f, 0.0f, 0.0f },
        (Vector2){ 200.0f, 200.0f },
        (Color){ 6, 12, 28, 255 }
    );

    /* ── Table body (dark-blue block) ────────────────────────────────────── */
    DrawCube(
        (Vector3){ 0.0f, w->tableY - 0.12f, 0.0f },
        w->tableW, 0.24f, w->tableL,
        (Color){ 12, 34, 65, 255 }
    );
    DrawCubeWires(
        (Vector3){ 0.0f, w->tableY - 0.12f, 0.0f },
        w->tableW, 0.24f, w->tableL,
        C_CYAN
    );

    /* ── Table surface (teal inset, flush with top of table body) ─────────── */
    DrawCube(
        (Vector3){ 0.0f, w->tableY + 0.012f, 0.0f },
        w->tableW - 0.12f, 0.025f, w->tableL - 0.12f,
        (Color){ 8, 55, 80, 255 }
    );

    /* ── Net body (translucent, spans the net line at Z=0) ───────────────── */
    DrawCube(
        (Vector3){ 0.0f, w->tableY + 0.12f, 0.0f },
        w->tableW, 0.24f, 0.04f,
        (Color){ 35, 180, 220, 95 }   /* alpha=95 → subtle cyan glow */
    );

    /* ── Net rail (solid cyan bar atop the net) ───────────────────────────── */
    DrawCube(
        (Vector3){ 0.0f, w->tableY + 0.245f, 0.0f },
        w->tableW, 0.025f, 0.05f,
        C_CYAN
    );

    /* ── Longitudinal reference grid lines on the table surface ─────────── */
    for (int i = -2; i <= 2; i++) {
        float fi = (float)i;
        DrawLine3D(
            (Vector3){ fi * 0.8f, w->tableY + 0.03f, -w->tableL / 2.0f + 0.1f },
            (Vector3){ fi * 0.8f, w->tableY + 0.03f,  w->tableL / 2.0f - 0.1f },
            (Color){ 30, 125, 160, 80 }
        );
    }

    /* ── Player paddle (cyan) ────────────────────────────────────────────── */
    DrawCube(
        (Vector3){ w->player.position.x,
                   w->player.position.y,
                   w->player.position.z },
        w->player.halfWidth  * 2.0f,
        w->player.halfHeight * 2.0f,
        0.18f,
        C_CYAN
    );
    DrawCubeWires(
        (Vector3){ w->player.position.x,
                   w->player.position.y,
                   w->player.position.z },
        w->player.halfWidth  * 2.0f,
        w->player.halfHeight * 2.0f,
        0.18f,
        RAYWHITE
    );

    /* ── AI paddle (pink) ────────────────────────────────────────────────── */
    DrawCube(
        (Vector3){ w->ai.position.x,
                   w->ai.position.y,
                   w->ai.position.z },
        w->ai.halfWidth  * 2.0f,
        w->ai.halfHeight * 2.0f,
        0.18f,
        C_PINK
    );
    DrawCubeWires(
        (Vector3){ w->ai.position.x,
                   w->ai.position.y,
                   w->ai.position.z },
        w->ai.halfWidth  * 2.0f,
        w->ai.halfHeight * 2.0f,
        0.18f,
        RAYWHITE
    );

    /* ── Ball + motion trail (only when ball is in play) ─────────────────── */
    BallBody *b = &w->ball;
    if (b->inPlay) {

        /* Primary ball sphere */
        DrawSphere(b->position, b->radius, C_GREEN);
        DrawSphereWires(b->position, b->radius, 8, 8, RAYWHITE);

        /* Motion trail: 8 ghost spheres behind the ball.
         * Ghost position: ballPos - velocity * (0.018 * i)
         *   The 0.018 constant was tuned so the tail spans roughly one
         *   frame of travel at typical ball speeds (~2–5 units/s).
         * Ghost radius shrinks linearly: ballRadius * (1 - i/10)
         * Ghost alpha fades  linearly: 160 - i*16  → 0 at i=10 */
        for (int i = 1; i <= 8; i++) {
            Vector3 tail = V3Sub(b->position, V3Scale(b->velocity, 0.018f * (float)i));
            DrawSphere(
                tail,
                b->radius * (1.0f - (float)i / 10.0f),
                (Color){ 50, 255, 210, (unsigned char)(160 - i * 16) }
            );
        }
    }

    /* ── Particle system ─────────────────────────────────────────────────── */
    for (size_t i = 0; i < g->anim.count; i++) {
        Particle *p = &g->anim.particles[i];

        /* Normalised lifetime fraction: 1.0 when fresh, 0.0 when dead. */
        float f = ClampF(p->life / p->maxLife, 0.0f, 1.0f);

        /* Copy the particle colour so we can override alpha without mutating
         * the stored colour (important for multi-frame fade correctness). */
        Color c = p->color;
        c.a = (unsigned char)(255.0f * f);  /* Fade out as particle ages */

        DrawSphere(p->pos, p->size * f, c); /* Shrink and fade simultaneously */
    }
}

/* =============================================================================
 * EngineDraw
 * =============================================================================
 * Renders the complete current frame.  Called once per rendered frame (not
 * once per physics tick).  Contains ZERO state mutation.
 *
 * RENDERING LAYERS (per state)
 * ----------------------------
 *
 * STATE_MATCH / STATE_PAUSE (shared 3-D base):
 *   1. ClearBackground
 *   2. Build a per-frame camera copy with shake applied:
 *      - Add g->anim.cameraKick to position  (hit juice)
 *      - Add sinusoidal jitter if g->anim.shake > 0  (smash screen-shake)
 *        offsetX = sin(time * 67) * shake * 0.25
 *        offsetY = cos(time * 79) * shake * 0.15
 *        Different frequencies (67, 79 Hz) on each axis prevent boring
 *        purely-horizontal or purely-vertical shake patterns.
 *   3. BeginMode3D / DrawArena / EndMode3D
 *   4. Top gradient fade (makes score text legible over 3-D background)
 *   5. Score display centred on screen
 *   6. Rally counter, control hint bar
 *   7. Smash charge bar (shown only while g->charging)
 *
 * STATE_PAUSE (additional overlay on top of match base):
 *   8. Semi-transparent full-screen dark rect
 *   9. PAUSED title
 *   10. Resume and Main Menu buttons (UIButtonDraw called HERE, not in Update)
 *
 * STATE_MENU:
 *   Animated floating particle dots (cosmetic), title, subtitle, version tag.
 *   Menu buttons (UIButtonDraw called HERE for hover glow animations).
 *
 * STATE_TUTORIAL / STATE_LEADERBOARD / STATE_GAME_OVER:
 *   Title + static text panels.
 *
 * Parameters:
 *   g — pointer to the live GameEngine (logically read-only).
 * ============================================================================= */
void EngineDraw(GameEngine *g)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    ClearBackground(C_BG);

    /* ── STATE_MATCH or STATE_PAUSE — shared 3-D viewport ───────────────── */
    if (g->core.state == STATE_MATCH || g->core.state == STATE_PAUSE) {

        /* Build a per-frame camera copy; never mutate g->camera directly. */
        Camera3D cam = g->camera;

        /* Apply animation camera-kick (recoil from a hard hit). */
        cam.position = V3Add(cam.position, g->anim.cameraKick);

        /* Apply screen-shake: two independent sinusoidal offsets on X and Y.
         * Using different angular frequencies (67, 79) breaks the regularity
         * and makes the shake feel more organic.  Shake decays over time
         * inside AnimUpdate() so no reset is needed here. */
        if (g->anim.shake > 0.0f) {
            cam.position.x += sinf(g->anim.time * 67.0f) * g->anim.shake * 0.25f;
            cam.position.y += cosf(g->anim.time * 79.0f) * g->anim.shake * 0.15f;
        }

        BeginMode3D(cam);
            DrawArena(g);
        EndMode3D();

        /* Top gradient: fades from 59 % opaque black to transparent so the
         * score HUD is readable against any 3-D background colour. */
        DrawRectangleGradientV(0, 0, sw, 150,
            (Color){ 0, 0, 0, 150 },
            (Color){ 0, 0, 0, 0 }
        );

        /* ── Score display ────────────────────────────────────────────────── */
        char score[64];
        snprintf(score, sizeof(score),
                 "%d     :     %d", g->playerScore, g->aiScore);
        DrawText(score,
                 sw / 2 - MeasureText(score, 42) / 2, 28, 42, RAYWHITE);
        DrawText("YOU", sw / 2 - 145, 40, 16, C_CYAN);
        DrawText("CPU", sw / 2 + 105, 40, 16, C_PINK);

        /* ── Rally counter ────────────────────────────────────────────────── */
        DrawText(TextFormat("RALLY %d", g->rally), 24, 24, 20, C_GREEN);

        /* ── Control hint strip along the bottom ─────────────────────────── */
        DrawText(
            "MOVE MOUSE  /  LEFT CLICK: DRIVE  /  HOLD RIGHT CLICK: SMASH",
            24, sh - 36, 16, (Color){ 165, 195, 225, 255 }
        );

        /* ── Smash charge bar (only while charging) ───────────────────────── */
        if (g->charging) {
            DrawRectangle(24, sh - 65, 220, 10, (Color){ 30, 40, 60, 255 });   /* Background */
            DrawRectangle(24, sh - 65, (int)(220.0f * g->charge), 10, C_PINK); /* Filled portion */
            DrawText("SMASH CHARGE", 254, sh - 70, 14, C_PINK);
        }

        /* ── STATE_PAUSE overlay ─────────────────────────────────────────── */
        if (g->core.state == STATE_PAUSE) {

            /* Semi-transparent full-screen dimmer. */
            DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 170 });

            UIDrawTitle("PAUSED", "Press ESC to resume", g->anim.time);

            /*
             * Draw the pause menu buttons.  UIButtonDraw() is called HERE (in
             * the draw phase) — NOT in EngineUpdate().  The click results are
             * intentionally ignored because EngineUpdate() already processed
             * the click via CheckCollisionPointRec.  We call UIButtonDraw()
             * purely for the visual hover-glow animation.
             */
            float cxPause = (float)(sw / 2 - 130);
            UIButton bResume = { { cxPause, 430.0f, 260.0f, 58.0f }, "RESUME",    0.0f, true };
            UIButton bMenu   = { { cxPause, 510.0f, 260.0f, 58.0f }, "MAIN MENU", 0.0f, true };
            UIButtonDraw(&bResume, GetMousePosition(), GetFrameTime());
            UIButtonDraw(&bMenu,   GetMousePosition(), GetFrameTime());
        }

    /* ── STATE_MENU ──────────────────────────────────────────────────────── */
    } else if (g->core.state == STATE_MENU) {

        /* Animated background dots: each dot follows a pseudo-random path
         * computed by wrapping (index-based offset + time * speed) with fmodf.
         * No heap allocation needed — positions are computed analytically. */
        for (int i = 0; i < 35; i++) {
            float x = fmodf((float)i * 137.0f + g->anim.time * (float)(12 + i % 3) * 8.0f,
                            (float)sw);
            float y = fmodf((float)i * 83.0f  + g->anim.time * (float)( 7 + i % 4) * 5.0f,
                            (float)sh);
            DrawCircle((int)x, (int)y, (float)(1 + i % 3),
                       (Color){ 35, 150, 200, (unsigned char)(30 + (i % 6) * 8) });
        }

        UIDrawTitle("NEON TABLE TENNIS",
                    "A little spin. A lot of attitude.", g->anim.time);
        DrawText("3D ARCADE EDITION", sw / 2 - 100, 245, 18, C_PINK);

        /*
         * Menu buttons: UIButtonDraw() is called here in the draw phase.
         * The return value (clicked) is IGNORED — click handling happened in
         * EngineUpdate() via CheckCollisionPointRec.  These calls are purely
         * for drawing the hover-glow animation on each button.
         */
        Vector2 mouse = GetMousePosition();
        float   fdt   = GetFrameTime();
        float   cx    = (float)(sw / 2 - 140);

        UIButton bStart       = { { cx, 390.0f, 280.0f, 60.0f }, "START MATCH",  0.0f, true };
        UIButton bHowToPlay   = { { cx, 470.0f, 280.0f, 60.0f }, "HOW TO PLAY",  0.0f, true };
        UIButton bLeaderboard = { { cx, 550.0f, 280.0f, 60.0f }, "LEADERBOARD",  0.0f, true };
        UIButton bQuit        = { { cx, 630.0f, 280.0f, 60.0f }, "QUIT",         0.0f, true };

        UIButtonDraw(&bStart,       mouse, fdt);
        UIButtonDraw(&bHowToPlay,   mouse, fdt);
        UIButtonDraw(&bLeaderboard, mouse, fdt);
        UIButtonDraw(&bQuit,        mouse, fdt);

    /* ── STATE_TUTORIAL ──────────────────────────────────────────────────── */
    } else if (g->core.state == STATE_TUTORIAL) {

        UIDrawTitle("HOW TO PLAY",
                    "Simple controls. Sneaky physics.", g->anim.time);

        const char *lines[] = {
            "MOVE YOUR MOUSE to guide the paddle across your side.",
            "LEFT CLICK when the ball reaches your paddle to drive it back.",
            "HOLD RIGHT CLICK, then release for a charged smash.",
            "Offset hits add spin. The ball curves as it travels.",
            "First player to 7 points wins. Press ENTER to return."
        };

        for (int i = 0; i < 5; i++) {
            /* Highlight the smash instruction in pink for emphasis. */
            DrawText(lines[i],
                     sw / 2 - 330, 330 + i * 48, 20,
                     (i == 2) ? C_PINK : RAYWHITE);
        }

    /* ── STATE_LEADERBOARD ───────────────────────────────────────────────── */
    } else if (g->core.state == STATE_LEADERBOARD) {

        ScoreRecord r = DBGetBest(&g->db);

        UIDrawTitle("LOCAL LEADERBOARD", "Saved on this PC", g->anim.time);
        DrawText(TextFormat("BEST SCORE: %d",            r.score),
                 sw / 2 - 140, 350, 26, C_CYAN);
        DrawText(TextFormat("LONGEST RALLY: %d",         r.rallies),
                 sw / 2 - 140, 400, 22, RAYWHITE);
        DrawText(TextFormat("WINS: %d     LOSSES: %d",   r.wins, r.losses),
                 sw / 2 - 140, 445, 22, RAYWHITE);
        DrawText("Click or press ENTER to return",
                 sw / 2 - 150, 550, 18, (Color){ 165, 195, 225, 255 });

    /* ── STATE_GAME_OVER ─────────────────────────────────────────────────── */
    } else if (g->core.state == STATE_GAME_OVER) {

        UIDrawTitle(
            g->matchWon ? "VICTORY!"   : "MATCH OVER",
            g->matchWon ? "Clean winner. Run it back?"
                        : "The next rally is yours to take.",
            g->anim.time
        );

        DrawText(TextFormat("FINAL SCORE   %d  :  %d",
                            g->playerScore, g->aiScore),
                 sw / 2 - 160, 350, 28, RAYWHITE);

        DrawText("Click or press ENTER for the main menu",
                 sw / 2 - 205, 470, 18, (Color){ 165, 195, 225, 255 });
    }
}
