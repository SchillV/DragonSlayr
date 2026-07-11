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
    int hook_depth = 0;        // item-hook re-entrancy guard (see items.cpp)

    // Call after content is loaded; spawns the player and enemies.
    void init_from_dungeon(DungeonResult d, uint64_t seed);
    void tick(const PlayerCmd& cmd, float dt);

    // Creates one enemy entity from a ContentDB::enemies index at a world
    // position. Reused by init_from_dungeon, the spawn console command, and
    // (later) floor transitions.
    entt::entity spawn_enemy(int def_index, glm::vec2 pos);

    // Creates a floor pickup for a ContentDB::items index.
    entt::entity spawn_pickup(int item_index, glm::vec2 pos);

    // Recomputes the player's StatBlock and syncs Health to it: raising max
    // HP heals by the gained amount, lowering it clamps. Call after granting
    // or removing stat modifiers (items, classes, floor effects).
    void refresh_player_stats();

    // Hot reload: swap definitions, remapping live entities by string id.
    // Entities whose def disappeared are destroyed.
    void apply_content(ContentDB new_content);

    const TileMap& map() const { return dungeon.map; }

    // Telemetry hooks — no-ops until the recorder lands (M5), but the sim
    // calls them from day one so nothing needs rewiring later.
    void on_player_dash(glm::vec2 dir);
};

// Weighted, deterministic spawn pick over any def roster with spawn_weight +
// min_floor (enemies, items, ...). Returns -1 if none are eligible. Pure (no
// World state) so it's unit-testable.
template <typename Def>
int pick_weighted_for_floor(const std::vector<Def>& defs, int floor, Rng& rng) {
    auto eligible = [&](const Def& d) { return d.min_floor <= floor && d.spawn_weight > 0.0f; };
    float total = 0.0f;
    for (const Def& d : defs) {
        if (eligible(d)) {
            total += d.spawn_weight;
        }
    }
    if (total <= 0.0f) {
        return -1;
    }
    float roll = rng.next_float01() * total;
    int last = -1;
    for (size_t i = 0; i < defs.size(); ++i) {
        if (!eligible(defs[i])) {
            continue;
        }
        last = static_cast<int>(i);
        roll -= defs[i].spawn_weight;
        if (roll <= 0.0f) {
            return last;
        }
    }
    return last; // float roundoff: fall back to the final eligible def
}

inline int pick_enemy_for_floor(const ContentDB& content, int floor, Rng& rng) {
    return pick_weighted_for_floor(content.enemies, floor, rng);
}

inline int pick_item_for_floor(const ContentDB& content, int floor, Rng& rng) {
    return pick_weighted_for_floor(content.items, floor, rng);
}

} // namespace ds
