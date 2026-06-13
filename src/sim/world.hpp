#pragma once

#include "core/rng.hpp"
#include "sim/content.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/player.hpp"
#include "sim/telemetry.hpp"

#include <entt/entt.hpp>

#include <cstdint>

namespace ds {

// The whole sim facade. Systems are free functions called in a fixed order
// inside tick() — no scheduler, no system objects. Never includes SDL.
struct World {
    entt::registry reg;
    DungeonResult dungeon;
    ContentDB content;
    TelemetryRecorder telem;
    Rng rng;
    uint64_t seed = 0;
    uint64_t tick_count = 0;

    entt::entity player = entt::null;
    float player_pitch = 0.0f; // view pitch from the last cmd (used for aiming)
    bool player_dead = false;
    int score = 0;
    int current_floor = 1;     // drives spawn-table eligibility (advances in the floors milestone)
    int primary_weapon = -1;   // "sword" — gear replaces this later
    int secondary_weapon = -1; // "bolt"

    // Call after content is loaded; spawns the player and enemies.
    void init_from_dungeon(DungeonResult d, uint64_t seed);
    void tick(const PlayerCmd& cmd, float dt);

    // Creates one enemy entity from a ContentDB::enemies index at a world
    // position. Reused by init_from_dungeon, the spawn console command, and
    // (later) floor transitions.
    entt::entity spawn_enemy(int def_index, glm::vec2 pos);

    // Hot reload: swap definitions, remapping live entities by string id.
    // Entities whose def disappeared are destroyed.
    void apply_content(ContentDB new_content);

    const TileMap& map() const { return dungeon.map; }

    // Telemetry hooks — no-ops until the recorder lands (M5), but the sim
    // calls them from day one so nothing needs rewiring later.
    void on_player_dash(glm::vec2 dir);
};

// Weighted, deterministic spawn pick: chooses an enemy def index whose
// min_floor <= floor by spawn_weight, drawing from `rng`. Returns -1 if none
// are eligible. Pure (no World state) so it's unit-testable.
int pick_enemy_for_floor(const ContentDB& content, int floor, Rng& rng);

} // namespace ds
