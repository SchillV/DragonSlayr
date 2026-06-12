#include <doctest/doctest.h>

#include "render/dungeon_mesh.hpp"

using namespace ds;

namespace {

TileMap make_map(int w, int h) {
    TileMap map;
    map.tiles = Grid2D<Tile>(w, h, Tile::Wall);
    return map;
}

} // namespace

TEST_CASE("single floor cell emits floor, ceiling and four walls") {
    TileMap map = make_map(3, 3);
    map.tiles.at(1, 1) = Tile::Floor;
    const MeshData mesh = build_dungeon_mesh(map);
    CHECK(mesh.vertices.size() == 6u * 4u); // 6 quads
    CHECK(mesh.indices.size() == 6u * 6u);
}

TEST_CASE("adjacent floor cells emit no wall on the shared edge") {
    TileMap map = make_map(3, 4);
    map.tiles.at(1, 1) = Tile::Floor;
    map.tiles.at(1, 2) = Tile::Floor;
    // 2 floors + 2 ceilings + 6 perimeter walls = 10 quads.
    const MeshData mesh = build_dungeon_mesh(map);
    CHECK(mesh.vertices.size() == 10u * 4u);
    CHECK(mesh.indices.size() == 10u * 6u);
}

TEST_CASE("map without floor emits nothing") {
    const MeshData mesh = build_dungeon_mesh(make_map(4, 4));
    CHECK(mesh.vertices.empty());
    CHECK(mesh.indices.empty());
}

TEST_CASE("all indices reference valid vertices") {
    TileMap map = make_map(5, 5);
    for (int y = 1; y < 4; ++y) {
        for (int x = 1; x < 4; ++x) {
            map.tiles.at(x, y) = Tile::Floor;
        }
    }
    const MeshData mesh = build_dungeon_mesh(map);
    for (const uint32_t idx : mesh.indices) {
        CHECK(idx < mesh.vertices.size());
    }
}
