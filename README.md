# Neon Table Tennis

> **A high-energy 3D arcade table tennis game for Windows, written in C with Raylib.**

Neon Table Tennis is a complete, public-release-quality arcade game featuring a Magnus-effect physics model, AI opponent, particle effects, camera shake, a persistent SQLite leaderboard, and a fully modular C11 codebase. All game visuals are rendered procedurally with Raylib primitives — no external textures are required to play.

---

## Screenshots

> *(Add screenshots here once compiled.)*

---

## Features

- **Arcade Physics Engine** — Semi-implicit Euler integration with gravity, quadratic air drag, Magnus effect (spin-induced curvature), energy-absorbing table bounce, net collision, and paddle-offset spin transfer.
- **Mouse-Guided Paddle** — Moving the mouse drives the 3D paddle position mapped from screen space into the world table plane via normalized device coordinate transformation.
- **Two Hit Types** — Left click for a standard drive; hold and release right click for a charged power smash with variable strength.
- **CPU Opponent** — Predicts the ball's landing point via forward simulation, then pursues it with bounded linear speed. Difficulty scales with match duration.
- **Juice & Feel** — Per-hit screen shake, camera recoil, slow-motion on smashes, glowing ball motion trail (8-frame history), and 2 000-particle burst system with O(1) spawn and removal.
- **Complete State Machine** — Main Menu → Tutorial → Arcade Match → Pause → Game Over → Leaderboard, with clean transitions and ESC navigation.
- **Persistent Leaderboard** — SQLite 3 local database (WAL mode) recording best score, longest rally, and cumulative win/loss totals. Database failure is non-fatal; the game remains fully playable.
- **Fixed-Timestep Loop** — Physics runs at a locked 60 Hz update rate decoupled from the variable render frame rate. A capped accumulator (max 8 sub-steps) prevents the spiral-of-death on slow frames.
- **Graceful Asset Fallback** — All audio files are optional. Missing WAV files log a warning and the game continues silently. All visuals use Raylib primitives.

---

## Controls

| Input | Action |
|---|---|
| **Mouse move** | Guide your paddle with high-sensitivity, spring-damped tracking |
| **Left click** | Drive (standard shot) |
| **Hold Right click, release** | Charged smash with buffered trigger window (hold longer = more power & topspin) |
| **Escape** | Pause / resume / return to menu |
| **Enter** or **click** | Advance tutorial, leaderboard, or game-over screen |

### Rules & Scoring
- **Configurable Match Length** — Choose your minimum score to win (**5**, **7**, **11**, or **21** points) directly from the Main Menu.
- **Table Tennis Rules** — The ball must bounce legally once on the receiver's court. If the receiver fails to return it (ball goes out after bouncing on receiver's court, double bounces, or dies on receiver's court), the **hitter wins the point**.
- **Faults** — Hitting the ball out of bounds without landing on opponent's table, bouncing on your own side, or illegal volleys over the table award the point to your opponent.
- **Win by 2 & Deuce** — Players must gain a 2-point margin to win. If tied at `target - 1`, the game enters deuce.
- **Service Alternation** — Service changes every 2 points during regulation play and every 1 point during deuce.

---

## Build Requirements

| Requirement | Version |
|---|---|
| Windows | 10 or 11 (64-bit) |
| C Compiler | MinGW-w64 GCC 12+ (e.g. MSYS2 UCRT64 or w64devkit) |
| Raylib | 5.x (installed in `C:/raylib/raylib` or custom path) |
| SQLite3 | 3.x development headers + library (e.g. MSYS2 UCRT64) |
| GNU Make | `mingw32-make` via MinGW / MSYS2 |

---

## Build with MinGW Make

The project uses a production-ready `Makefile` with zero CMake dependency. Run from the project root:

```powershell
# Release build (default: -O3, -DNDEBUG, strict warnings)
mingw32-make

# Debug build (symbols, assertions enabled: -g3, -O0, -DDEBUG)
mingw32-make BUILD=debug

# Run the game
mingw32-make run
# or directly:
.\neon_table_tennis.exe

# Clean build artifacts
mingw32-make clean
```

### Custom Dependency Paths

If your Raylib or SQLite installations reside in custom directories, override the default variables when invoking make:

```powershell
mingw32-make RAYLIB_PATH="D:/Libs/raylib" MSYS_PATH="D:/msys64/ucrt64"
```

The Makefile compiles with `-O3 -Wall -Wextra -Wpedantic -Wshadow -Wconversion` and links against `raylib`, `sqlite3`, `opengl32`, `gdi32`, `winmm`, and `m`.

---

## Optional Audio Assets

Place the following WAV files in `assets/audio/` to enable sound effects. The game runs silently if they are absent.

| File | Event |
|---|---|
| `paddle.wav` | Standard drive hit |
| `smash.wav` | Charged smash hit |
| `bounce.wav` | Ball bouncing off the table |
| `ui.wav` | Menu button click |

---

## Project Structure

```
neon_table_tennis/
├── include/
│   ├── core.h          Window management, global state machine, timing constants
│   ├── engine.h        Game loop coordinator, input dispatcher, state updates
│   ├── physics.h       Rigid-body kinematics, ball arc, Magnus, AI prediction
│   ├── math_3d.h       Inline vector math, reflection, interpolation utilities
│   ├── anim_engine.h   Particle pool, camera juice, screen shake, UI pulse
│   ├── audio_mgr.h     Sound bank, dynamic pitch modulation
│   ├── db_handler.h    SQLite3 wrapper, schema, prepared statements
│   └── ui_system.h     Immediate-mode button, panel, and title drawing
├── src/
│   ├── main.c                  Entry point, fixed-timestep accumulator loop
│   ├── core/core.c             Window init, frame begin/end, state transitions
│   ├── engine/engine.c         Gameplay logic, input routing, scene rendering
│   ├── physics/physics.c       Ball integration, collision, AI, scoring
│   ├── animation/anim_engine.c Particle system, hit juice, camera shake
│   ├── audio/audio_mgr.c       Sound loading, pitch modulation, fallback
│   ├── database/db_handler.c   SQLite schema creation, match recording, query
│   └── ui/ui_system.c          Button hover, panel draw, title animation
├── assets/
│   ├── audio/          Optional WAV sound effects (see above)
│   ├── fonts/          Reserved for custom fonts
│   └── shaders/        Reserved for GLSL post-processing shaders
├── Makefile
└── README.md
```

---

## Architecture Overview

### Fixed-Timestep Loop

`main.c` implements the classic fixed-timestep game loop:

```
accumulator += frame_time (capped at 250 ms)
while accumulator >= FIXED_DT (1/60 s) and steps < 8:
    PhysicsUpdate(FIXED_DT)
    accumulator -= FIXED_DT
if steps hit cap: accumulator = 0   ← discard debt, prevent spiral-of-death
BeginDrawing()
EngineDraw()                        ← render at native frame rate
EndDrawing()
```

Physics runs deterministically at 60 Hz regardless of display refresh rate. Rendering runs as fast as the GPU allows (target 144 Hz).

### Physics Model

The ball is integrated using **semi-implicit Euler** (velocity updated first from acceleration, then position updated from new velocity). This gives better energy preservation than explicit Euler at the same cost:

```
a_gravity = (0, -9.81, 0)
a_drag    = -drag_coeff * |v| * v          (quadratic drag)
a_magnus  = magnus_coeff * (spin × v)      (Magnus cross product)
a_total   = a_gravity + a_drag + a_magnus

v += a_total * dt                          ← velocity first
x += v * dt                               ← then position
spin *= (1 - spin_decay * dt)             ← exponential spin decay
```

Paddle contact computes the **normalized contact offset** `(hit_pos - paddle_center) / paddle_half_extent`. This offset drives lateral velocity and angular momentum transfer to simulate edge spin.

### AI Algorithm

At each physics tick the AI:
1. Runs `PhysicsPredictLanding` — a lightweight forward simulation at 120 Hz sub-steps that finds where the ball will next reach table height.
2. Adds random error proportional to `(1 - difficulty)` to simulate imperfect prediction.
3. Moves toward the target at speed `3.2 * difficulty` units/s, clamped to the table width.

Difficulty ramps from 0.58 to 0.92 over the first 150 seconds of play: `difficulty = clamp(0.58 + matchTime / 150, 0.58, 0.92)`.

### Particle System

The particle pool is **preallocated** at startup (`AnimInit` with capacity 1 200). Spawning is O(1): write to `particles[count++]` while under capacity, or overwrite a random slot when full. Removal is O(1): swap the dead particle with `particles[--count]`.

### State Machine

```
STATE_MENU ──[Start Match]──► STATE_MATCH ──[ESC]──► STATE_PAUSE
     │                              │                      │
 [Tutorial]                  [7 points]             [Resume/Menu]
     ▼                              ▼
STATE_TUTORIAL            STATE_GAME_OVER ──[Enter/Click]──► STATE_MENU
     │
[Enter/Click]──► STATE_MENU

STATE_MENU ──[Leaderboard]──► STATE_LEADERBOARD ──[Enter/Click]──► STATE_MENU
STATE_MENU ──[Quit]──► STATE_QUIT (exits main loop)
Any non-menu + ESC ──► STATE_MENU
```

---

## Code Quality Standards

- **C11** with zero VLAs or compiler extensions (`-Wpedantic` clean).
- **`-Wall -Wextra -Wshadow -Wconversion`** — all warnings treated as quality signals.
- **No circular dependencies** between modules. Dependency order: `math_3d` → `physics`, `anim_engine`, `audio_mgr`, `ui_system`, `db_handler` → `engine` → `main`.
- **Detailed block comments** on every function explaining the algorithm, formula, and parameters.
- **Proper memory management** — `AnimFree` frees the particle pool; `AudioFree` unloads sounds and closes the audio device; `DBClose` checkpoints WAL and closes the SQLite connection.
- **SQLite WAL mode** — Write-Ahead Logging allows future read concurrency and reduces corruption risk.

---

## License

This project does not currently ship with a license. **Add a license file before distributing binaries or accepting contributions.**

Raylib is distributed under the **zlib license**. SQLite3 is **public domain**.
