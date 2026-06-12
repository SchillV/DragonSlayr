#include "sim/pathfinding.hpp"

#include "sim/collision.hpp"

#include <algorithm>
#include <queue>

namespace ds {

namespace {

struct OpenNode {
    int f = 0;
    int order = 0; // stable tie-break keeps the search deterministic
    glm::ivec2 tile{};

    bool operator>(const OpenNode& o) const {
        return f != o.f ? f > o.f : order > o.order;
    }
};

int manhattan(glm::ivec2 a, glm::ivec2 b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

} // namespace

std::vector<glm::ivec2> astar(const TileMap& map, glm::ivec2 start, glm::ivec2 goal,
                              int max_expansions) {
    if (map.solid(start.x, start.y) || map.solid(goal.x, goal.y)) {
        return {};
    }
    if (start == goal) {
        return {start};
    }

    Grid2D<int> g_cost(map.width(), map.height(), -1);
    Grid2D<glm::ivec2> came_from(map.width(), map.height(), glm::ivec2{-1, -1});
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<>> open;

    int order = 0;
    g_cost.at(start.x, start.y) = 0;
    open.push({manhattan(start, goal), order++, start});

    constexpr glm::ivec2 kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int expansions = 0;

    while (!open.empty() && expansions < max_expansions) {
        const OpenNode node = open.top();
        open.pop();
        ++expansions;

        if (node.tile == goal) {
            std::vector<glm::ivec2> path{goal};
            glm::ivec2 t = goal;
            while (t != start) {
                t = came_from.at(t.x, t.y);
                path.push_back(t);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        const int g = g_cost.at(node.tile.x, node.tile.y);
        for (const glm::ivec2 d : kDirs) {
            const glm::ivec2 n = node.tile + d;
            if (map.solid(n.x, n.y)) {
                continue;
            }
            const int ng = g + 1;
            int& slot = g_cost.at(n.x, n.y);
            if (slot >= 0 && slot <= ng) {
                continue;
            }
            slot = ng;
            came_from.at(n.x, n.y) = node.tile;
            open.push({ng + manhattan(n, goal), order++, n});
        }
    }
    return {};
}

bool segment_clear(const TileMap& map, glm::vec2 a, glm::vec2 b, float radius) {
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-6f) {
        return true;
    }
    const glm::vec2 dir = d / len;
    const glm::vec2 perp{-dir.y, dir.x};
    // Center line plus both body edges — cheap and good enough on a grid.
    for (const float side : {0.0f, radius, -radius}) {
        const glm::vec2 off = perp * side;
        if (grid_raycast(map, a + off, b + off)) {
            return false;
        }
    }
    return true;
}

std::vector<glm::vec2> smooth_path(const TileMap& map, std::span<const glm::ivec2> tiles,
                                   float radius) {
    std::vector<glm::vec2> points;
    points.reserve(tiles.size());
    for (const glm::ivec2 t : tiles) {
        points.push_back({static_cast<float>(t.x) + 0.5f, static_cast<float>(t.y) + 0.5f});
    }
    if (points.size() <= 2) {
        return points;
    }

    std::vector<glm::vec2> out{points.front()};
    size_t anchor = 0;
    while (anchor + 1 < points.size()) {
        size_t furthest = anchor + 1;
        for (size_t j = points.size() - 1; j > anchor; --j) {
            if (segment_clear(map, points[anchor], points[j], radius)) {
                furthest = j;
                break;
            }
        }
        out.push_back(points[furthest]);
        anchor = furthest;
    }
    return out;
}

} // namespace ds
