#include "sim/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ds {

namespace {

constexpr float kSkin = 1e-4f; // keeps the body strictly outside the wall plane

bool body_overlaps_solid(const TileMap& map, glm::vec2 c, float r) {
    const int x0 = static_cast<int>(std::floor(c.x - r));
    const int x1 = static_cast<int>(std::floor(c.x + r));
    const int y0 = static_cast<int>(std::floor(c.y - r));
    const int y1 = static_cast<int>(std::floor(c.y + r));
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            if (map.solid(tx, ty)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

MoveResult slide_move(const TileMap& map, glm::vec2 pos, float radius, glm::vec2 delta) {
    MoveResult result{pos, false, false};
    const float len = glm::length(delta);
    if (len <= 0.0f) {
        return result;
    }
    // Substeps small enough that one step can never cross a full tile.
    const int steps = std::max(1, static_cast<int>(std::ceil(len / std::max(radius * 0.5f, 0.05f))));
    const glm::vec2 step = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        if (step.x != 0.0f) {
            result.pos.x += step.x;
            if (body_overlaps_solid(map, result.pos, radius)) {
                if (step.x > 0.0f) {
                    result.pos.x = std::floor(result.pos.x + radius) - radius - kSkin;
                } else {
                    result.pos.x = std::floor(result.pos.x - radius) + 1.0f + radius + kSkin;
                }
                result.hit_x = true;
            }
        }
        if (step.y != 0.0f) {
            result.pos.y += step.y;
            if (body_overlaps_solid(map, result.pos, radius)) {
                if (step.y > 0.0f) {
                    result.pos.y = std::floor(result.pos.y + radius) - radius - kSkin;
                } else {
                    result.pos.y = std::floor(result.pos.y - radius) + 1.0f + radius + kSkin;
                }
                result.hit_y = true;
            }
        }
    }
    return result;
}

bool grid_raycast(const TileMap& map, glm::vec2 a, glm::vec2 b, glm::ivec2* hit_tile) {
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-6f) {
        return false;
    }
    const glm::vec2 dir = d / len;

    glm::ivec2 tile{static_cast<int>(std::floor(a.x)), static_cast<int>(std::floor(a.y))};
    const glm::ivec2 end{static_cast<int>(std::floor(b.x)), static_cast<int>(std::floor(b.y))};

    const int step_x = dir.x > 0.0f ? 1 : -1;
    const int step_y = dir.y > 0.0f ? 1 : -1;
    constexpr float kInf = std::numeric_limits<float>::max();
    const float tdx = dir.x != 0.0f ? std::abs(1.0f / dir.x) : kInf;
    const float tdy = dir.y != 0.0f ? std::abs(1.0f / dir.y) : kInf;
    float tmx = dir.x != 0.0f
                    ? (dir.x > 0.0f ? (static_cast<float>(tile.x) + 1.0f - a.x) : (a.x - static_cast<float>(tile.x))) * tdx
                    : kInf;
    float tmy = dir.y != 0.0f
                    ? (dir.y > 0.0f ? (static_cast<float>(tile.y) + 1.0f - a.y) : (a.y - static_cast<float>(tile.y))) * tdy
                    : kInf;

    for (int guard = 0; guard < 4096; ++guard) {
        if (tile == end) {
            return false;
        }
        float t;
        if (tmx < tmy) {
            tile.x += step_x;
            t = tmx;
            tmx += tdx;
        } else {
            tile.y += step_y;
            t = tmy;
            tmy += tdy;
        }
        if (t > len) {
            return false;
        }
        if (map.solid(tile.x, tile.y)) {
            if (hit_tile) {
                *hit_tile = tile;
            }
            return true;
        }
    }
    return false;
}

} // namespace ds
