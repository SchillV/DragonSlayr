#pragma once

#include "core/rng.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/player.hpp"

#include <entt/entt.hpp>

#include <cstdint>

namespace ds {

// The whole sim facade. Systems are free functions called in a fixed order
// inside tick() — no scheduler, no system objects. Never includes SDL.
struct World {
    entt::registry reg;
    DungeonResult dungeon;
    Rng rng;
    uint64_t seed = 0;
    uint64_t tick_count = 0;

    entt::entity player = entt::null;
    float player_pitch = 0.0f; // view pitch from the last cmd (used for aiming)

    void init_from_dungeon(DungeonResult d, uint64_t seed);
    void tick(const PlayerCmd& cmd, float dt);

    const TileMap& map() const { return dungeon.map; }

    // Telemetry hooks — no-ops until the recorder lands (M5), but the sim
    // calls them from day one so nothing needs rewiring later.
    void on_player_dash(glm::vec2 dir);
};

} // namespace ds
