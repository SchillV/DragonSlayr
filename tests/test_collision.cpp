#include <doctest/doctest.h>

#include "sim/collision.hpp"

using namespace ds;
using doctest::Approx;

namespace {

// w x h map: Floor interior with a Wall ring.
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

constexpr float kR = 0.3f;

} // namespace

TEST_CASE("movement into a wall clamps at the boundary") {
    const TileMap map = walled_room(6, 6);
    const MoveResult res = slide_move(map, {3.0f, 3.0f}, kR, {10.0f, 0.0f});
    CHECK(res.hit_x);
    CHECK_FALSE(res.hit_y);
    CHECK(res.pos.x == Approx(5.0f - kR).epsilon(0.01));
    CHECK(res.pos.y == Approx(3.0f));
}

TEST_CASE("sliding along a wall preserves tangential motion") {
    const TileMap map = walled_room(6, 6);
    // Push hard into the east wall while drifting north: x clamps, y completes.
    const MoveResult res = slide_move(map, {3.0f, 2.5f}, kR, {10.0f, 1.0f});
    CHECK(res.hit_x);
    CHECK_FALSE(res.hit_y);
    CHECK(res.pos.x == Approx(5.0f - kR).epsilon(0.01));
    CHECK(res.pos.y == Approx(3.5f));
}

TEST_CASE("no tunneling at absurd speeds") {
    const TileMap map = walled_room(8, 8);
    const MoveResult res = slide_move(map, {1.5f, 4.0f}, kR, {500.0f, 0.0f});
    CHECK(res.hit_x);
    CHECK(res.pos.x == Approx(7.0f - kR).epsilon(0.01));
}

TEST_CASE("walking parallel to a wall does not snag on tile corners") {
    const TileMap map = walled_room(8, 8);
    // Hug the north wall (body edge a hair off the boundary) and run east.
    const float y = 1.0f + kR + 0.01f;
    const MoveResult res = slide_move(map, {1.5f, y}, kR, {4.0f, 0.0f});
    CHECK_FALSE(res.hit_x);
    CHECK_FALSE(res.hit_y);
    CHECK(res.pos.x == Approx(5.5f));
    CHECK(res.pos.y == Approx(y));
}

TEST_CASE("diagonal into a corner clamps both axes") {
    const TileMap map = walled_room(6, 6);
    const MoveResult res = slide_move(map, {2.0f, 2.0f}, kR, {-5.0f, -5.0f});
    CHECK(res.hit_x);
    CHECK(res.hit_y);
    CHECK(res.pos.x == Approx(1.0f + kR).epsilon(0.01));
    CHECK(res.pos.y == Approx(1.0f + kR).epsilon(0.01));
}

TEST_CASE("out of bounds counts as solid") {
    TileMap map;
    map.tiles = Grid2D<Tile>(4, 4, Tile::Floor); // no wall ring at all
    const MoveResult res = slide_move(map, {2.0f, 2.0f}, kR, {10.0f, 0.0f});
    CHECK(res.hit_x);
    CHECK(res.pos.x == Approx(4.0f - kR).epsilon(0.01));
}

TEST_CASE("grid_raycast sees clear lines and hits walls") {
    TileMap map = walled_room(8, 8);

    CHECK_FALSE(grid_raycast(map, {1.5f, 1.5f}, {6.5f, 1.5f}));
    CHECK_FALSE(grid_raycast(map, {1.5f, 1.5f}, {6.5f, 6.5f}));

    map.tiles.at(4, 1) = Tile::Wall; // pillar in the row
    glm::ivec2 hit{};
    CHECK(grid_raycast(map, {1.5f, 1.5f}, {6.5f, 1.5f}, &hit));
    CHECK(hit == glm::ivec2{4, 1});

    // Ray leaving the map hits the boundary wall.
    CHECK(grid_raycast(map, {1.5f, 5.5f}, {20.0f, 5.5f}, &hit));
    CHECK(hit == glm::ivec2{7, 5});
}

TEST_CASE("zero-length movement is a no-op") {
    const TileMap map = walled_room(6, 6);
    const MoveResult res = slide_move(map, {3.0f, 3.0f}, kR, {0.0f, 0.0f});
    CHECK_FALSE(res.hit_x);
    CHECK_FALSE(res.hit_y);
    CHECK(res.pos.x == Approx(3.0f));
    CHECK(res.pos.y == Approx(3.0f));
}
