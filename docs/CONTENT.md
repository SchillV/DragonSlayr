# Content cookbook

How to add and tune content. Read [ARCHITECTURE.md](ARCHITECTURE.md) first for the big picture.

## Philosophy: data for variants, code for new primitives

DragonSlayr is **hybrid** data-driven. The intent:

- **New variants and combinations are pure JSON** — a tougher enemy, a faster weapon, a different
  damage number: edit a file in `assets/data/`, no recompile. While the game is running these
  files **hot-reload** (the change applies within ~half a second).
- **Genuinely new mechanics need a small amount of C++** — a brand-new *kind* of behavior (a
  ranged enemy, a piercing projectile, an item effect) adds one entry to a curated palette in
  code, after which JSON can use and tune it freely.

The sections below are ordered from "pure data" to "needs code." Each names the exact files.

> **Status note.** The slice currently ships **one enemy behavior** (a melee chaser) and **two
> weapons** (sword, bolt). Items, classes, room types, and floor progression are **not
> implemented yet** — their sections describe the planned hooks and are marked _(planned)_. The
> data pipeline, hot-reload, and validation they'll use already exist.

---

## Recipe: tune or add a weapon — *pure JSON, works today*

File: `assets/data/weapons.json`. Each key is a weapon id. Two `type`s are supported and
dispatched today: `"melee"` (arc swing) and `"projectile"` (travels, hits the first thing).

```json
"flamebolt": {
  "name": "Flame Bolt",
  "type": "projectile",
  "damage": 9,
  "speed": 18.0,
  "radius": 0.15,
  "cooldown_s": 0.7,
  "ttl_s": 2.5,
  "sprite": "bolt",
  "viewmodel": "hand",
  "sound": "bolt_fire"
}
```

Required: `type`, `damage`. Everything else has a default (see `WeaponDef` in
`src/sim/content.hpp`). Melee uses `range`/`arc_deg`/`swing_s`; projectiles use
`speed`/`radius`/`ttl_s`. `sprite` is a texture name in `assets/textures/` (without extension; a
missing file falls back to a procedural glow, and `"bolt"` specifically has a built-in glow).

The player's starting loadout is currently fixed to the ids `"sword"` and `"bolt"` in
`World::init_from_dungeon` (`src/sim/world.cpp`) — rename those lookups to hand the player a
different weapon, or wait for the _(planned)_ class system to make loadouts data-driven.

## Recipe: tune or add an enemy — *stats are JSON today, with two caveats*

File: `assets/data/enemies.json`. Each key is an enemy id.

```json
"brute": {
  "name": "Dungeon Brute",
  "hp": 40,
  "speed": 2.2,
  "radius": 0.45,
  "aggro_radius": 12.0,
  "sprite": "monster",
  "sprite_size": [1.3, 1.3],
  "score": 150,
  "attack": { "damage": 14, "range": 1.1, "windup_s": 0.6, "cooldown_s": 1.4 }
}
```

Required: `hp`, `sprite`. This fully defines the enemy's *stats* and is hot-reloadable. **Two
honest caveats in the current slice:**

1. **Behavior is fixed.** Every enemy runs the one melee-chaser state machine in
   `src/sim/enemy_ai.cpp` (idle → chase → windup → recover). Your `brute` will be a bigger,
   tougher, slower walker — but it can't yet be ranged or stationary. Adding behaviors is the next
   section.
2. **Only `"walker"` actually spawns.** `World::init_from_dungeon` currently places a `"walker"`
   at every spawn point, so a new id won't appear in the dungeon on its own. To see it *now*,
   temporarily change that lookup (or rename your enemy to `"walker"`). The proper fix —
   **per-floor weighted spawn tables** so any defined enemy appears by data — is the first item on
   the roadmap below.

## Recipe: tune the feel — *cvars, live*

Movement, camera, and sensitivity are cvars, not defs. Defaults live in `assets/data/player.json`
(e.g. `sv.run_speed`, `sv.dash_speed`, `r.fov`, `in.sensitivity`). Edit that file, or change them
live in the in-game console (grave key): type `sv.run_speed 10` to test, `help` to list
everything. Console-set cvars don't persist; put values you like into `player.json`.

## Recipe: rebind keys — *pure JSON*

File: `assets/data/bindings.json`, mapping actions to SDL key names (e.g. `"dash": "Space"`). See
`src/platform/input.cpp` for the action list and defaults.

---

## Adding a genuinely new enemy behavior — *small C++*

When data isn't enough (a caster that keeps its distance and throws bolts, a turret that never
moves), add a behavior to the curated set:

1. In `src/sim/components.hpp`, extend `enum class AiState` if your behavior needs new states.
2. In `src/sim/enemy_ai.cpp::enemy_ai_think`, branch on the new behavior and implement its
   states. The existing chaser is the worked example; pathfinding (`astar`, `smooth_path`) and
   line-of-sight (`grid_raycast`) helpers are already there.
3. Add a `behavior` field to `EnemyDef` (`src/sim/content.hpp`) and parse it in
   `parse_enemy` (`src/sim/content.cpp`) so JSON can select it. (This `behavior` field is the
   intended near-term addition; until it exists, behavior is implicit.)

That's the hybrid pattern: one new C++ branch, then unlimited data-defined variants of it.

## Adding a new content category in C++ — *the ContentDB pattern*

To introduce a whole new kind of content (the way `items` will be added), follow the existing
enemy/weapon pattern in `src/sim/content.{hpp,cpp}`:

1. Define a `FooDef` struct in `content.hpp` and add `std::vector<FooDef> foos;` plus
   `int find_foo(std::string_view) const;` to `ContentDB`.
2. Write `parse_foo(...)` in `content.cpp` using the `read_float`/`read_string`/`read_int`
   helpers (they accumulate the JSON key path into error messages for free), and
   `load_foos_from_string` / `load_foos` mirroring the enemy loaders.
3. Call `content.load_foos(...)` from `load_content` in `src/game/app.cpp`, and add the file to
   the hot-reload poll list there.
4. Give entities that reference it a component holding the **index** (`uint16_t`), and remap it in
   `World::apply_content` so hot-reload keeps working.

This is intentionally repetitive today; reducing that boilerplate (a generic loader) and a
stat/modifier backbone for items and classes are planned foundation work.

---

## Roadmap _(planned — not yet implemented)_

These are designed seams, not finished features. Listed in the intended order of work.

- **Enemy variety** — a `behavior` field on `EnemyDef` (chaser / ranged / charger / stationary)
  dispatched in `enemy_ai.cpp`, plus **per-floor weighted spawn tables** so any enemy you define
  appears in the dungeon by data. This is what makes "add an enemy" truly JSON-only.
- **Items + synergies** — an `ItemDef` (stat modifiers + tagged effect hooks like `on_hit` /
  `on_kill` / passive), an `Inventory` component, pickup entities placed by the generator
  (`DungeonResult` already carries spawn-point lists), and a stat/modifier layer that recomputes a
  `Stats` cache from base values + held items. The core of build diversity.
- **Levels & rooms** — tagged `RoomType` defs (arena / treasure / ambush) chosen during
  generation, the already-present `exit_pos` wired up as functional **stairs**, and per-floor
  difficulty scaling driving the spawn tables above.
- **Player classes** — a `ClassDef` selected per run: starting loadout, base stats, and perks.
  Cheap once the stat/modifier layer from "items" exists.

When these land, this document's _(planned)_ markers come off and each gets a concrete recipe.
