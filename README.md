# DragonSlayr

A first-person action roguelike: delve a magical dungeon, slay the dragon, take the treasure.
Fast movement like the old shooters, melee and magic, procedural floors, and bosses that learn
from how you fought the ones before them.

This is a ground-up C++20 rewrite (SDL3 + SDL_GPU) of the original Python/Tkinter raycaster,
which lives on unchanged in [`legacy/`](legacy/).

**Status: playable combat slice.** Procedural dungeon, fast first-person movement with dash,
sword + arcane bolt combat against pathfinding walkers, HUD, debug tooling, and per-run
combat telemetry (the future boss-learning input). No floors/bosses/items yet.

## Controls

| Input | Action |
|---|---|
| WASD + mouse | move + look (running is the default speed) |
| Left Shift | walk (precision) |
| Space | dash |
| LMB / RMB | sword swing / arcane bolt |
| R | restart after death |
| F1 / F2 | toggle mouse capture / debug overlay |
| ` (grave) | console — `help`, `regen <seed>`, `noclip`, `sv.*` tuning cvars |
| Esc | quit |

Movement, combat and enemy numbers live in `assets/data/*.json` (hot-reloaded while the
game runs) and in cvars tunable from the console.

## Building

All third-party dependencies (SDL3, EnTT, GLM, Dear ImGui, miniaudio, stb, nlohmann/json,
doctest, and the glslang shader compiler) are fetched and pinned automatically at configure
time — nothing to install by hand. The **first** configure+build compiles SDL and glslang from
source and takes a few minutes; later builds are fast.

Prerequisites: **CMake ≥ 3.28**, a **C++20 compiler**, and a Vulkan-capable GPU/driver to *run*
the windowed game (the build itself needs no GPU). Shaders compile to SPIR-V at build time; if
the glslang build ever fails on your machine, configure with `-DDS_USE_PREBUILT_SHADERS=ON` to
use the committed `.spv` artifacts in `shaders/compiled/` instead.

### Linux (Ninja)

Needs Ninja and — for the *windowed* build — the usual X11/Wayland dev headers
(`libx11-dev`, `libxext-dev`, `libwayland-dev`, …; see the SDL3 docs).

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/dragonslayr
```

### Windows (Visual Studio 2022)

No extra dependencies — rendering uses Vulkan through SDL_GPU. With the "Desktop development
with C++" workload installed:

```bat
cmake --preset windows
cmake --build --preset windows-release
ctest --preset windows-release
build\windows\Release\dragonslayr.exe
```

(You can also use the Ninja presets on Windows from a developer command prompt if you prefer.)

### Headless (no window or GPU)

Used by the test suite and display-less CI containers; runs the simulation with a null renderer.

```sh
cmake --preset headless && cmake --build --preset headless && ctest --preset headless
./build/headless/dragonslayr --headless --ticks 600 --seed 1
```

## Understanding & extending the code

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — how the codebase is organized and why: the
  sim/render split, the fixed-timestep loop, the ECS and system order, the data/content
  pipeline, the renderer passes, and the telemetry system.
- **[docs/CONTENT.md](docs/CONTENT.md)** — the content cookbook: step-by-step recipes for adding
  enemies, weapons, and (as they land) items, classes, and room/floor types — what's editable as
  pure JSON today versus what currently needs a small C++ change.

## Repository layout

```
src/core/      zero-dependency utilities (logging, rng, cvars)
src/sim/       headless game logic — never includes SDL (built as the ds_sim library)
src/platform/  SDL-facing: window, input, audio
src/render/    SDL_GPU renderer + debug UI
src/game/      main loop, session glue, HUD
shaders/       GLSL → SPIR-V (compiled at build time; fallbacks in shaders/compiled/)
assets/data/   JSON content definitions (enemies, weapons, player tuning, key bindings)
assets/        textures + sounds
tests/         doctest unit tests (link the sim only)
docs/          architecture overview + content-authoring cookbook
legacy/        the original Python/Tkinter game, kept runnable:
               cd legacy && python3 Run_Game.py
```
