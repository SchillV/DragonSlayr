#pragma once

#include "sim/tilemap.hpp"

#include <glm/glm.hpp>

namespace ds {

struct MoveResult {
    glm::vec2 pos{0.0f};
    bool hit_x = false;
    bool hit_y = false;
};

// Axis-separated slide against the tile grid with substepping (tunnel-proof up
// to absurd speeds). The body is an axis-aligned square of half-extent
// `radius` — classic grid-shooter collision: free wall-sliding, no corner
// snags. Out-of-bounds counts as solid.
MoveResult slide_move(const TileMap& map, glm::vec2 pos, float radius, glm::vec2 delta);

// lodev-style DDA over the grid. True if a solid tile lies strictly between
// `a` and `b`; the tile is written to hit_tile. `a`'s own tile is not tested.
bool grid_raycast(const TileMap& map, glm::vec2 a, glm::vec2 b, glm::ivec2* hit_tile = nullptr);

} // namespace ds
