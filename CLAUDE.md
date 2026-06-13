# CLAUDE.md

Context for AI coding sessions on DragonSlayr. Human docs: `README.md`, `docs/ARCHITECTURE.md`,
`docs/CONTENT.md`.

## What this is

A first-person action roguelike in C++20 (SDL3 + SDL_GPU), a ground-up rewrite of the Python
raycaster preserved in `legacy/`. Current state: a playable combat slice (procedural dungeon,
fast movement + dash, sword + arcane bolt, A*-chasing walkers, HUD, debug tools, per-run combat
telemetry). Built across milestones M0–M5.

## Build & test

```sh
cmake --preset headless && cmake --build --preset headless && ctest --preset headless
```

- `headless` is the container/CI preset (SDL with no desktop video; runs the sim with a null
  renderer). `release`/`debug` are Ninja desktop presets; `windows` is the Visual Studio 2022
  multi-config preset. First build compiles SDL + glslang from source (minutes); later builds are
  fast. `-DDS_USE_PREBUILT_SHADERS=ON` skips glslang and uses `shaders/compiled/*.spv`.
- The dev container has **no GPU and no display** — GPU/render code compiles here but only *runs*
  on the user's machine. Verify via the headless smoke test; the user verifies visuals/feel.
- Headless smoke: `dragonslayr --headless --ticks 1800 --seed 42 --bot walk_attack --telemetry-dir <dir>`.

## Architecture rules (don't break these)

- **`ds_sim` (src/core + src/sim) must never include SDL.** It's a separate library with no SDL
  on its include path; gameplay logic lives here and must stay headless-runnable and
  deterministic. Rendering/audio/window code lives in `src/render`, `src/platform`, `src/game`.
- **Fixed 60 Hz sim, interpolated render.** The sim only sees `PlayerCmd`. Don't read input or
  wall-clock time inside `src/sim`.
- **Systems are free functions in a fixed order** in `World::tick` (`src/sim/world.cpp`). Add a
  function and slot it into that list; don't introduce a scheduler/system-base-class framework.
- **One RNG**: `ds::Rng` (PCG32, `src/core/rng.hpp`). Generation determinism is golden-hash
  tested — adding RNG calls in `dungeon_gen` in a different order will break that test on purpose.
- **Content is data**: defs in `assets/data/*.json` → `ContentDB` (`src/sim/content.*`);
  components store integer indices into it; `World::apply_content` hot-reloads by id. Tuning that
  should be live goes through `cvar` (`src/core/cvar.hpp`), seeded from `player.json`.
- **Telemetry** (`src/sim/telemetry.*`) is the boss-learning input contract — keep recording
  def-ids + positions; don't change the event schema lightly.

## Conventions

- C++20, `-Wall -Wextra` (MSVC `/W4`); keep it warning-clean. Match surrounding style (comments
  explain *why*, not *what*). Keep modules small and replaceable over framework-y abstraction.
- Prefer editing JSON/cvars over hardcoding. New mechanics: add to the curated palette in C++,
  then expose via data (see `docs/CONTENT.md`, "hybrid" philosophy).
- Cross-platform: resources are found by walking up from the exe (`find_resource_dir`); shaders
  are staged next to the exe by a POST_BUILD step. Avoid OS-specific calls; localize the few that
  exist (e.g. `gmtime_s`/`gmtime_r` in `telemetry.cpp`).

## Git / workflow

- Develop on the feature branch `claude/optimistic-goldberg-lv6v3d`; commit per milestone with
  descriptive messages; commit identity `Claude <noreply@anthropic.com>`.
- **Known infra issue:** in the web environment the git proxy pushes under a fixed identity that
  currently lacks write access (push 403 "denied to SchillV"); a user-pasted PAT in the URL is
  *not* consumed by the proxy. Pushing needs the Claude Code GitHub integration to be granted
  write access to `schillv/dragonslayr`. Commit locally regardless.

## Roadmap (agreed next work, in order)

Foundation for scalability: generic content loader + a stat/modifier layer, then **enemy variety**
(behavior field + per-floor weighted spawn tables — note today only `"walker"` spawns), **items +
synergies**, **levels & rooms** (room types + functional stairs via `exit_pos` + per-floor
scaling). **Player classes** deprioritized until the stat layer exists. See `docs/CONTENT.md`
roadmap.
