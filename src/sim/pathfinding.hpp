#pragma once

#include "sim/tilemap.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace ds {

// 4-directional A* over floor tiles (cardinal moves can't cut corners).
// Returns the tile path including start and goal, or empty if unreachable.
std::vector<glm::ivec2> astar(const TileMap& map, glm::ivec2 start, glm::ivec2 goal,
                              int max_expansions = 4096);

// Converts tiles to centers and greedily drops waypoints whose connecting
// segment is clear at the given body radius, so paths hug corners instead of
// staircasing.
std::vector<glm::vec2> smooth_path(const TileMap& map, std::span<const glm::ivec2> tiles,
                                   float radius);

// True if a body of `radius` can travel a->b without touching solid tiles.
bool segment_clear(const TileMap& map, glm::vec2 a, glm::vec2 b, float radius);

} // namespace ds
