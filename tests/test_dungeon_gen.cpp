#include <doctest/doctest.h>

#include "sim/dungeon_gen.hpp"

using namespace ds;

TEST_CASE("same seed produces identical dungeons") {
    GenParams p;
    p.seed = 42;
    const DungeonResult a = generate_dungeon(p);
    const DungeonResult b = generate_dungeon(p);
    CHECK(a.map.hash() == b.map.hash());
    CHECK(a.player_spawn == b.player_spawn);
    CHECK(a.exit_pos == b.exit_pos);
    CHECK(a.enemy_spawns == b.enemy_spawns);

    p.seed = 43;
    CHECK(generate_dungeon(p).map.hash() != a.map.hash());
}

TEST_CASE("golden seeds stay stable") {
    // If these fail, the generation order changed. That invalidates recorded
    // seeds and telemetry baselines — only bump the values deliberately.
    GenParams p;
    p.seed = 1;
    CHECK(generate_dungeon(p).map.hash() == 13641166731860102257ULL);
    p.seed = 7;
    CHECK(generate_dungeon(p).map.hash() == 17867000263537485696ULL);
}

TEST_CASE("every floor tile is reachable from spawn") {
    for (const uint64_t seed : {1ULL, 2ULL, 3ULL, 4ULL, 5ULL}) {
        GenParams p;
        p.seed = seed;
        const DungeonResult d = generate_dungeon(p);
        REQUIRE(!d.rooms.empty());
        const Grid2D<int> dist = bfs_distances(d.map, d.player_spawn);
        for (int y = 0; y < d.map.height(); ++y) {
            for (int x = 0; x < d.map.width(); ++x) {
                if (d.map.tiles.at(x, y) == Tile::Floor) {
                    CHECK(dist.at(x, y) >= 0);
                }
            }
        }
    }
}

TEST_CASE("rooms respect bounds and size params") {
    GenParams p;
    p.seed = 9;
    const DungeonResult d = generate_dungeon(p);
    CHECK(static_cast<int>(d.rooms.size()) <= p.rooms_max);
    CHECK(!d.rooms.empty());
    for (const Room& r : d.rooms) {
        CHECK(r.w >= p.room_min);
        CHECK(r.w <= p.room_max);
        CHECK(r.h >= p.room_min);
        CHECK(r.h <= p.room_max);
        CHECK(r.x >= 1);
        CHECK(r.y >= 1);
        CHECK(r.x + r.w <= p.width - 1);
        CHECK(r.y + r.h <= p.height - 1);
    }
}

TEST_CASE("spawn and exit are on floor and not adjacent") {
    for (const uint64_t seed : {11ULL, 12ULL, 13ULL}) {
        GenParams p;
        p.seed = seed;
        const DungeonResult d = generate_dungeon(p);
        CHECK(d.map.tiles.at(d.player_spawn.x, d.player_spawn.y) == Tile::Floor);
        CHECK(d.map.tiles.at(d.exit_pos.x, d.exit_pos.y) == Tile::Floor);
        const Grid2D<int> dist = bfs_distances(d.map, d.player_spawn);
        CHECK(dist.at(d.exit_pos.x, d.exit_pos.y) >= 8);
    }
}

TEST_CASE("enemy spawns sit on floor tiles away from the player spawn") {
    GenParams p;
    p.seed = 21;
    const DungeonResult d = generate_dungeon(p);
    CHECK(!d.enemy_spawns.empty());
    for (const glm::ivec2 s : d.enemy_spawns) {
        CHECK(d.map.tiles.at(s.x, s.y) == Tile::Floor);
        CHECK(s != d.player_spawn);
        CHECK(s != d.exit_pos);
    }
}
