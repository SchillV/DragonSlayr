# DragonSlayr

A first-person action roguelike: delve a magical dungeon, slay the dragon, take the treasure.
Fast movement like the old shooters, melee and magic, procedural floors, and bosses that learn
from how you fought the ones before them.

This is a ground-up C++20 rewrite (SDL3 + SDL_GPU) of the original Python/Tkinter raycaster,
which lives on unchanged in [`legacy/`](legacy/).

**Status: early foundation work — not a game yet.**

## Building

Requires CMake ≥ 3.28, Ninja, and a C++20 compiler. All third-party dependencies are fetched
and pinned automatically at configure time (first configure+build takes a few minutes).

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Run the game:

```sh
./build/release/dragonslayr
```

Headless mode (no window/GPU — used by the test suite and CI containers):

```sh
./build/release/dragonslayr --headless --ticks 600 --seed 1
```

On display-less machines (CI containers with partial/absent X11 headers), use the `headless`
preset instead, which builds SDL without desktop video backends:

```sh
cmake --preset headless && cmake --build --preset headless && ctest --preset headless
```

Platform notes:
- **Linux**: building a *windowed* binary needs the usual X11/Wayland development headers
  (`libx11-dev`, `libwayland-dev`, ... — see SDL3 docs). Headless builds need nothing.
- **Windows**: no extra dependencies. Rendering uses Vulkan via SDL_GPU.

## Repository layout

```
src/core/      zero-dependency utilities (logging, rng, cvars)
src/sim/       headless game logic — never includes SDL
src/platform/  SDL-facing: window, input, audio
src/render/    SDL_GPU renderer + debug UI
src/game/      main loop, session glue, HUD
shaders/       GLSL → SPIR-V (compiled at build time)
assets/        data definitions (JSON) + textures + sounds
tests/         doctest unit tests (link the sim only)
legacy/        the original Python/Tkinter game, kept runnable:
               cd legacy && python3 Run_Game.py
```
