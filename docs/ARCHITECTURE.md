# Architecture

This document explains how DragonSlayr is put together so you can find your way around and know
where new code belongs. For step-by-step "how do I add an enemy/weapon/item" recipes, see
[CONTENT.md](CONTENT.md).

## The one split that matters: sim vs. everything else

The single most important structural rule: **game logic never touches SDL or the GPU.** It is
enforced by the build, not by discipline.

- **`ds_sim`** (a static library: `src/core/` + `src/sim/`) is the simulation — movement,
  collision, AI, combat, dungeon generation, content loading, telemetry. SDL is *not* on its
  include path, so if sim code ever `#include`s SDL it fails to compile. The unit tests link
  only this library.
- **`dragonslayr`** (the executable: `src/platform/` + `src/render/` + `src/game/` + `main.cpp`)
  is the shell around the sim: window, input, audio, the SDL_GPU renderer, and the debug UI.

Why this matters: the simulation is fully deterministic and runs **headless** (no window, no GPU)
at thousands of ticks per second. That is what lets the test suite and CI containers exercise the
*real* game logic — generate a floor, spawn enemies, run the combat loop, write telemetry — on
machines with no graphics at all. When you add gameplay, it goes in `src/sim/`; when you add a way
to *see or hear* it, that goes in `src/render/` or `src/platform/`.

```
main.cpp ──> game/app.cpp ──> sim/world.cpp (the simulation)
                   │                  ▲
                   ├─ platform/  (SDL: window, input, audio)
                   └─ render/    (SDL_GPU renderer, ImGui debug UI)
```

## Directory map

| Path | Role |
|---|---|
| `src/core/` | Zero-dependency utilities: `log`, `rng` (PCG32 — the project's only RNG), `cvar` (console variables + commands), `grid` (`Grid2D<T>`). |
| `src/sim/` | The simulation. `world` (the facade), `tilemap`, `dungeon_gen`, `collision`, `pathfinding`, `player`, `enemy_ai`, `combat`, `components`, `content` (data defs + JSON loaders), `telemetry`, `bot` (headless input). |
| `src/platform/` | SDL-facing: `platform` (init/window/resource discovery), `input` (action mapping), `audio` (miniaudio). |
| `src/render/` | `renderer` (the `IRenderer` interface + `NullRenderer`), `gpu_device`, `gpu_renderer`, `dungeon_mesh` (pure CPU mesh builder), `texture_load`, `debug_ui`, `frame_view`. |
| `src/game/` | `app` (the main loop and session glue), `hud`. |
| `assets/data/` | JSON content: `enemies.json`, `weapons.json`, `player.json` (cvar tuning), `bindings.json`. |
| `shaders/` | GLSL 450 sources compiled to SPIR-V at build time. |
| `tests/` | doctest unit tests, linking `ds_sim` only. |

## The main loop (fixed timestep)

`App::run_windowed` (`src/game/app.cpp`) runs a classic fixed-timestep loop:

```
accumulate real time
while (accumulator >= 1/60):   world.tick(cmd, 1/60);   accumulator -= 1/60
render( interpolate(prev, current, accumulator) )
```

- The simulation **always** advances in fixed 1/60 s steps, so physics and combat are
  deterministic and frame-rate independent.
- Rendering interpolates between the previous and current tick using the left-over accumulator
  (`alpha`), so motion is smooth at any display rate. Every moving entity carries a
  `PrevTransform` (copied at the top of each tick) for this.
- **Mouse-look is sampled at render rate** and passed *into* the next tick's command, so aiming
  has zero perceived latency even though the sim is locked to 60 Hz.
- `App::run_headless` runs the same `world.tick` with no window, no renderer, and no pacing —
  driven by a scripted `Bot` instead of live input — as fast as the CPU allows.

The only thing the sim ever learns about input is a `PlayerCmd` (`src/sim/player.hpp`): movement
wish-direction, look angles, and button states. Live input, the headless bot, and any future demo
playback all just produce `PlayerCmd`s — which is why the sim is so easy to test.

## The ECS and the tick order

The simulation state lives in an [EnTT](https://github.com/skypjack/entt) registry owned by
`World` (`src/sim/world.hpp`). Components are plain structs in `src/sim/components.hpp`
(`Transform`, `Velocity`, `Body`, `Health`, `Player`, `Enemy`, `Projectile`, `HurtFlash`, …).

Systems are **free functions called in a fixed order** — there is no scheduler, no system base
class, no hidden magic. `World::tick` reads top to bottom:

```
copy_prev_transforms      // snapshot for render interpolation
player_apply_cmd          // movement: friction, accelerate, dash
player_combat             // sword swing / bolt spawn from the current weapon
enemy_ai_think            // per-enemy state machine (idle → chase → windup → recover)
move_and_collide          // swept circle-vs-grid for everything with a Body
enemy_separation          // push overlapping enemies apart
projectiles_update        // move bolts, test walls/enemies, apply damage
(telemetry move sample)   // 4 Hz player position/velocity sample
cleanup_dead              // destroy entities tagged this tick
```

To add a behavior, you write a free function and call it from this list at the right point. To
understand a behavior, you read it top to bottom — the order *is* the design.

## Data & content pipeline

Content is **data-driven**: enemies and weapons are defined in `assets/data/*.json`, parsed into
`EnemyDef` / `WeaponDef` structs in a `ContentDB` (`src/sim/content.hpp`). Key ideas:

- **String ids intern to indices.** A def's JSON key (`"walker"`) becomes its `id`; components
  store a small integer index into the `ContentDB` vector, not a pointer. This keeps components
  tiny and makes hot-reload possible.
- **Hot reload.** While the game runs, `app.cpp` polls the data files' modification times; on a
  change it reparses and calls `World::apply_content`, which remaps live entities by id (an enemy
  whose def changed keeps running with the new numbers; one whose def was deleted despawns).
- **Errors name the offending field.** The loader helpers (`read_float`/`read_string`/…) report
  failures with the full JSON key path (e.g. `enemies.walker.attack.damage: expected a number`)
  and leave the existing content untouched, so a typo never half-loads a roster.

Player/camera tuning (`sv.run_speed`, `r.fov`, …) is exposed as **cvars** (`src/core/cvar.hpp`)
rather than defs: they're live-tunable from the in-game console (grave key) and seeded from
`assets/data/player.json`. Cvars flagged `CVAR_CHEAT` (e.g. `noclip`, `god`) mark the run so the
telemetry/leaderboard can disqualify it.

See [CONTENT.md](CONTENT.md) for how this pipeline is used in practice and where it's headed.

## Rendering

The renderer is deliberately dumb — all logic is in the sim; the renderer just draws a
`FrameView` (`src/render/frame_view.hpp`), an interpolated, GPU-agnostic snapshot of the frame.
The `IRenderer` interface has a `NullRenderer` implementation so headless runs take the exact same
code path with no GPU.

`GpuRenderer` (SDL_GPU, SPIR-V → Vulkan) draws one main pass with three pipelines, then ImGui:

1. **World** — the dungeon as a single textured mesh (built on the CPU by `dungeon_mesh.cpp` from
   the tile grid), with per-face shading and exponential distance fog for the retro look.
2. **Sprites** — instanced **billboards** for enemies and projectiles: world-up-locked
   (cylindrical, so they don't tilt when you look up/down) with alpha-discard + depth-write, so no
   sorting is ever needed and they clip correctly against walls.
3. **Overlay** — 2D screen-space quads (depth off, alpha blended): the weapon viewmodel and the
   HUD (crosshair, health bar, hurt flash).

Shaders are GLSL compiled to SPIR-V at build time by a from-source glslang; a POST_BUILD step
stages the `.spv` files beside the executable so they're found on every build layout. SDL_GPU has
strict SPIR-V binding-set conventions (vertex samplers `set=0`/UBOs `set=1`, fragment samplers
`set=2`/UBOs `set=3`) — there's a reminder comment at the top of every shader.

## Telemetry (the boss-learning seam)

Every combat-relevant action — attacks, projectile fires/hits, damage taken, dashes, kills (with
time-alive), and a 4 Hz movement sample — is recorded into a ring buffer
(`src/sim/telemetry.hpp`) during play and persisted as versioned JSON on death/restart/quit (to
the OS pref path, or `--telemetry-dir`). Events carry def-id strings and positions, so future
aggregation (melee-vs-ranged ratio, engagement distance, dodge-after-windup rate, …) needs no
schema change. This is the input contract for the planned boss-learning system: floor bosses will
read the current run's events live; the final dragon will read the cross-run history. Nothing
about recording will change when that lands — the data is already being captured.

## Determinism & RNG

Same build + same seed ⇒ same dungeon and same simulation. Dungeon generation uses a single
PCG32 stream (`src/core/rng.hpp`) with stable iteration order and no unordered containers; a
golden-hash unit test guards against accidental drift. (Cross-*platform* float-identical
determinism is explicitly **not** a goal — telemetry is statistical, there's no lockstep
netcode.)

## Cross-platform notes

Windows and Linux build from the same sources (see the README for commands). The portability
points worth knowing:

- **Resource discovery** walks up from the executable to find `shaders/` and `assets/`
  (`find_resource_dir` in `src/platform/platform.cpp`), so it works whether the exe sits at
  `build/release/` (Ninja) or `build/windows/Release/` (Visual Studio). Compiled shaders are
  staged next to the exe by a POST_BUILD step.
- **`main`** uses `<SDL3/SDL_main.h>`, which supplies the correct Windows entry point.
- **Audio** (miniaudio) talks to the OS directly — WASAPI on Windows, ALSA/PulseAudio on Linux —
  so there are no SDL audio dependencies.
- Platform-divergent code is rare and localized (e.g. the `gmtime_s`/`gmtime_r` split in
  `telemetry.cpp`).
