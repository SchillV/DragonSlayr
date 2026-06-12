#include <doctest/doctest.h>

#include "sim/pathfinding.hpp"

#include <cstdlib>

using namespace ds;

namespace {

TileMap walled_room(int w, int h) {
    TileMap map;
    map.tiles = Grid2D<Tile>(w, h, Tile::Floor);
    for (int x = 0; x < w; ++x) {
        map.tiles.at(x, 0) = Tile::Wall;
        map.tiles.at(x, h - 1) = Tile::Wall;
    }
    for (int y = 0; y < h; ++y) {
        map.tiles.at(0, y) = Tile::Wall;
        map.tiles.at(w - 1, y) = Tile::Wall;
    }
    return map;
}

} // namespace

TEST_CASE("straight corridor gives the manhattan path") {
    const TileMap map = walled_room(8, 3); // floor row y=1, x in [1,6]
    const auto path = astar(map, {1, 1}, {6, 1});
    REQUIRE(path.size() == 6);
    CHECK(path.front() == glm::ivec2{1, 1});
    CHECK(path.back() == glm::ivec2{6, 1});
}

TEST_CASE("walls force the optimal detour") {
    TileMap map = walled_room(7, 7);
    // Vertical wall at x=3 blocking rows y=1..3; the only way around is y=4.
    for (int y = 1; y <= 3; ++y) {
        map.tiles.at(3, y) = Tile::Wall;
    }
    const auto path = astar(map, {1, 1}, {5, 1});
    REQUIRE(!path.empty());
    // Manhattan distance is 4; the detour adds 2 * 3 rows = 6 extra steps.
    CHECK(path.size() == 11);
}

TEST_CASE("unreachable goals return empty") {
    TileMap map = walled_room(9, 9);
    // Seal a chamber around the goal.
    for (int y = 4; y <= 6; ++y) {
        for (int x = 4; x <= 6; ++x) {
            map.tiles.at(x, y) = Tile::Wall;
        }
    }
    map.tiles.at(5, 5) = Tile::Floor;
    CHECK(astar(map, {1, 1}, {5, 5}).empty());
}

TEST_CASE("start equals goal") {
    const TileMap map = walled_room(5, 5);
    const auto path = astar(map, {2, 2}, {2, 2});
    REQUIRE(path.size() == 1);
    CHECK(path[0] == glm::ivec2{2, 2});
}

TEST_CASE("paths only take cardinal steps (no corner cutting possible)") {
    TileMap map = walled_room(10, 10);
    map.tiles.at(4, 4) = Tile::Wall;
    map.tiles.at(5, 5) = Tile::Wall;
    const auto path = astar(map, {1, 1}, {8, 8});
    REQUIRE(!path.empty());
    for (size_t i = 1; i < path.size(); ++i) {
        const glm::ivec2 d = path[i] - path[i - 1];
        CHECK(std::abs(d.x) + std::abs(d.y) == 1);
    }
}

TEST_CASE("smooth_path collapses straight corridors to two points") {
    const TileMap map = walled_room(8, 3);
    const auto tiles = astar(map, {1, 1}, {6, 1});
    const auto smooth = smooth_path(map, tiles, 0.3f);
    REQUIRE(smooth.size() == 2);
    CHECK(smooth.front().x == doctest::Approx(1.5f));
    CHECK(smooth.back().x == doctest::Approx(6.5f));
}

TEST_CASE("smoothed paths stay clear at body radius") {
    TileMap map = walled_room(10, 10);
    for (int y = 1; y <= 6; ++y) {
        map.tiles.at(5, y) = Tile::Wall; // an L-detour
    }
    const auto tiles = astar(map, {2, 2}, {8, 2});
    REQUIRE(!tiles.empty());
    const float radius = 0.3f;
    const auto smooth = smooth_path(map, tiles, radius);
    REQUIRE(smooth.size() >= 2);
    CHECK(smooth.size() <= tiles.size());
    for (size_t i = 1; i < smooth.size(); ++i) {
        CHECK(segment_clear(map, smooth[i - 1], smooth[i], radius));
    }
}
