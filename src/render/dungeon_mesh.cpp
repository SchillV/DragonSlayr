#include "render/dungeon_mesh.hpp"

namespace ds {

namespace {

// Legacy-raycaster-style face shading: X-facing walls darker than Z-facing.
constexpr float kShadeFloor = 0.85f;
constexpr float kShadeCeiling = 0.70f;
constexpr float kShadeWallZ = 1.00f;
constexpr float kShadeWallX = 0.80f;

void add_quad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, float layer,
              float shade) {
    const auto base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, {0.0f, 0.0f}, layer, shade});
    m.vertices.push_back({b, {1.0f, 0.0f}, layer, shade});
    m.vertices.push_back({c, {1.0f, 1.0f}, layer, shade});
    m.vertices.push_back({d, {0.0f, 1.0f}, layer, shade});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

MeshData build_dungeon_mesh(const TileMap& map, float wall_height) {
    MeshData m;
    const float wh = wall_height;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            if (map.tiles.at(x, y) != Tile::Floor) {
                continue;
            }
            const auto fx = static_cast<float>(x);
            const auto fz = static_cast<float>(y);

            // Floor (visible from above) and ceiling (visible from below).
            add_quad(m, {fx, 0.0f, fz}, {fx, 0.0f, fz + 1.0f}, {fx + 1.0f, 0.0f, fz + 1.0f},
                     {fx + 1.0f, 0.0f, fz}, kLayerFloor, kShadeFloor);
            add_quad(m, {fx, wh, fz}, {fx + 1.0f, wh, fz}, {fx + 1.0f, wh, fz + 1.0f},
                     {fx, wh, fz + 1.0f}, kLayerCeiling, kShadeCeiling);

            // One wall face per solid cardinal neighbor, facing this floor tile.
            if (map.solid(x, y - 1)) { // north edge (z = fz), faces +Z
                add_quad(m, {fx, wh, fz}, {fx + 1.0f, wh, fz}, {fx + 1.0f, 0.0f, fz},
                         {fx, 0.0f, fz}, kLayerWall, kShadeWallZ);
            }
            if (map.solid(x, y + 1)) { // south edge (z = fz+1), faces -Z
                add_quad(m, {fx + 1.0f, wh, fz + 1.0f}, {fx, wh, fz + 1.0f}, {fx, 0.0f, fz + 1.0f},
                         {fx + 1.0f, 0.0f, fz + 1.0f}, kLayerWall, kShadeWallZ);
            }
            if (map.solid(x - 1, y)) { // west edge (x = fx), faces +X
                add_quad(m, {fx, wh, fz + 1.0f}, {fx, wh, fz}, {fx, 0.0f, fz},
                         {fx, 0.0f, fz + 1.0f}, kLayerWall, kShadeWallX);
            }
            if (map.solid(x + 1, y)) { // east edge (x = fx+1), faces -X
                add_quad(m, {fx + 1.0f, wh, fz}, {fx + 1.0f, wh, fz + 1.0f},
                         {fx + 1.0f, 0.0f, fz + 1.0f}, {fx + 1.0f, 0.0f, fz}, kLayerWall,
                         kShadeWallX);
            }
        }
    }
    return m;
}

} // namespace ds
