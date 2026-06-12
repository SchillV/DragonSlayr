#pragma once

#include "render/renderer.hpp"
#include "sim/tilemap.hpp"

namespace ds {

// Texture array layers used by the world atlas.
inline constexpr float kLayerWall = 0.0f;
inline constexpr float kLayerFloor = 1.0f;
inline constexpr float kLayerCeiling = 2.0f;

// Pure CPU: one quad per floor/ceiling tile plus one wall face per
// floor-to-solid edge. Tile (x, y) maps to world x/z; +Y is up.
MeshData build_dungeon_mesh(const TileMap& map, float wall_height = 1.0f);

} // namespace ds
