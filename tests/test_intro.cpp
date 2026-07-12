#include <doctest/doctest.h>

#include "game/intro.hpp"
#include "sim/components.hpp"
#include "sim/world.hpp"

using namespace ds;

namespace {

constexpr const char* kEnemies = R"({"walker": {"hp": 12, "speed": 3.0, "sprite": "m",
    "score": 50, "attack": {"damage": 6, "range": 0.9}}})";

} // namespace

TEST_CASE("the approach map is walkable from spawn to the cave mouth") {
    const DungeonResult d = intro_approach_map();
    CHECK(d.map.tiles.at(d.player_spawn.x, d.player_spawn.y) == Tile::Floor);
    CHECK(d.map.tiles.at(d.exit_pos.x, d.exit_pos.y) == Tile::Floor);

    const Grid2D<int> dist = bfs_distances(d.map, d.player_spawn);
    CHECK(dist.at(d.exit_pos.x, d.exit_pos.y) > 0); // reachable

    REQUIRE(d.enemy_spawns.size() == 2); // the two guards
    for (const glm::ivec2 g : d.enemy_spawns) {
        CHECK(d.map.tiles.at(g.x, g.y) == Tile::Floor);
        CHECK(dist.at(g.x, g.y) > 0);                              // on the path
        CHECK(dist.at(g.x, g.y) < dist.at(d.exit_pos.x, d.exit_pos.y)); // before the mouth
    }
}

TEST_CASE("stepping into the cave mouth raises the binding trigger") {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    world.init_from_dungeon(intro_approach_map(), 0x13270ULL);
    CHECK(world.reg.view<Enemy>().size() == 2); // guards spawned

    world.reg.get<Transform>(world.player).pos = {32.5f, 6.5f};
    world.tick({}, 1.0f / 60.0f);
    CHECK(world.floor_exit_requested); // the app turns this into the binding
}
