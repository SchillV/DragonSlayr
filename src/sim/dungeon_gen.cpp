#include "sim/dungeon_gen.hpp"

#include "core/rng.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace ds {

namespace {

bool rooms_overlap_with_gap(const Room& a, const Room& b) {
    // Expand by 1 so rooms always keep a full wall tile between them.
    return a.x - 1 < b.x + b.w + 1 && a.x + a.w + 1 > b.x - 1 && a.y - 1 < b.y + b.h + 1 &&
           a.y + a.h + 1 > b.y - 1;
}

void carve_room(TileMap& map, const Room& r) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            map.tiles.at(x, y) = Tile::Floor;
        }
    }
}

void carve_corridor(TileMap& map, glm::ivec2 a, glm::ivec2 b, bool x_first) {
    glm::ivec2 p = a;
    auto step_axis = [&](int& coord, int target) {
        while (coord != target) {
            coord += coord < target ? 1 : -1;
            map.tiles.at(p.x, p.y) = Tile::Floor;
        }
    };
    map.tiles.at(p.x, p.y) = Tile::Floor;
    if (x_first) {
        step_axis(p.x, b.x);
        step_axis(p.y, b.y);
    } else {
        step_axis(p.y, b.y);
        step_axis(p.x, b.x);
    }
}

struct Edge {
    int a = 0, b = 0;
    float len = 0.0f;
};

} // namespace

DungeonResult generate_dungeon(const GenParams& params) {
    DungeonResult out;
    Rng rng(params.seed);
    out.map.tiles = Grid2D<Tile>(params.width, params.height, Tile::Void);

    // 1) Rooms: rejection placement with a guaranteed 1-tile gap.
    const int target_rooms = rng.range_int(params.rooms_min, params.rooms_max);
    for (int attempt = 0; attempt < params.max_place_attempts &&
                          static_cast<int>(out.rooms.size()) < target_rooms;
         ++attempt) {
        Room r;
        r.w = rng.range_int(params.room_min, params.room_max);
        r.h = rng.range_int(params.room_min, params.room_max);
        r.x = rng.range_int(1, params.width - r.w - 2);
        r.y = rng.range_int(1, params.height - r.h - 2);
        const bool blocked = std::any_of(out.rooms.begin(), out.rooms.end(),
                                         [&](const Room& o) { return rooms_overlap_with_gap(r, o); });
        if (!blocked) {
            out.rooms.push_back(r);
        }
    }
    for (const Room& r : out.rooms) {
        carve_room(out.map, r);
    }

    // 2) Prim MST over room centers + a few short non-tree edges for loops.
    const int n = static_cast<int>(out.rooms.size());
    std::vector<Edge> tree_edges;
    std::vector<Edge> skipped_edges;
    if (n > 1) {
        std::vector<bool> in_tree(static_cast<size_t>(n), false);
        in_tree[0] = true;
        for (int added = 1; added < n; ++added) {
            Edge best{-1, -1, std::numeric_limits<float>::max()};
            for (int a = 0; a < n; ++a) {
                if (!in_tree[a]) continue;
                for (int b = 0; b < n; ++b) {
                    if (in_tree[b]) continue;
                    const glm::vec2 d = glm::vec2(out.rooms[a].center() - out.rooms[b].center());
                    const float len = d.x * d.x + d.y * d.y;
                    if (len < best.len) {
                        best = {a, b, len};
                    }
                }
            }
            in_tree[best.b] = true;
            tree_edges.push_back(best);
        }
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const bool used = std::any_of(tree_edges.begin(), tree_edges.end(), [&](const Edge& e) {
                    return (e.a == a && e.b == b) || (e.a == b && e.b == a);
                });
                if (!used) {
                    const glm::vec2 d = glm::vec2(out.rooms[a].center() - out.rooms[b].center());
                    skipped_edges.push_back({a, b, d.x * d.x + d.y * d.y});
                }
            }
        }
        std::sort(skipped_edges.begin(), skipped_edges.end(),
                  [](const Edge& l, const Edge& r) { return l.len < r.len; });
        const int extra =
            static_cast<int>(std::round(params.loop_edges * static_cast<float>(tree_edges.size())));
        for (int i = 0; i < extra && i < static_cast<int>(skipped_edges.size()); ++i) {
            tree_edges.push_back(skipped_edges[static_cast<size_t>(i)]);
        }
    }

    // 3) L-shaped corridors with a random elbow.
    for (const Edge& e : tree_edges) {
        carve_corridor(out.map, out.rooms[e.a].center(), out.rooms[e.b].center(), rng.chance(0.5f));
    }

    // 4) Wrap every floor tile in walls.
    for (int y = 0; y < params.height; ++y) {
        for (int x = 0; x < params.width; ++x) {
            if (out.map.tiles.at(x, y) != Tile::Void) continue;
            bool touches_floor = false;
            for (int dy = -1; dy <= 1 && !touches_floor; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (out.map.tiles.get_or(x + dx, y + dy, Tile::Void) == Tile::Floor) {
                        touches_floor = true;
                        break;
                    }
                }
            }
            if (touches_floor) {
                out.map.tiles.at(x, y) = Tile::Wall;
            }
        }
    }

    if (out.rooms.empty()) {
        return out; // degenerate params; callers treat an empty room list as failure
    }

    // 5) Spawn in room 0; exit in the BFS-farthest room (fallback: farthest tile).
    out.player_spawn = out.rooms[0].center();
    const Grid2D<int> dist = bfs_distances(out.map, out.player_spawn);
    int best_room = -1;
    int best_dist = -1;
    for (int i = 1; i < n; ++i) {
        const glm::ivec2 c = out.rooms[static_cast<size_t>(i)].center();
        const int d = dist.at(c.x, c.y);
        if (d > best_dist) {
            best_dist = d;
            best_room = i;
        }
    }
    if (best_room >= 0) {
        out.exit_pos = out.rooms[static_cast<size_t>(best_room)].center();
    } else {
        glm::ivec2 far = out.player_spawn;
        int far_d = 0;
        for (int y = 0; y < params.height; ++y) {
            for (int x = 0; x < params.width; ++x) {
                if (dist.at(x, y) > far_d) {
                    far_d = dist.at(x, y);
                    far = {x, y};
                }
            }
        }
        out.exit_pos = far;
    }

    // 6) Enemy spawn points: per-room area budget, skipping the spawn room.
    for (int i = 1; i < n; ++i) {
        const Room& r = out.rooms[static_cast<size_t>(i)];
        const int budget = std::clamp(r.area() / 16, 1, 6);
        for (int k = 0; k < budget; ++k) {
            const glm::ivec2 p{rng.range_int(r.x, r.x + r.w - 1), rng.range_int(r.y, r.y + r.h - 1)};
            if (out.map.tiles.at(p.x, p.y) != Tile::Floor) continue;
            if (p == out.exit_pos || p == out.player_spawn) continue;
            out.enemy_spawns.push_back(p);
        }
    }
    return out;
}

Grid2D<int> bfs_distances(const TileMap& map, glm::ivec2 start) {
    Grid2D<int> dist(map.width(), map.height(), -1);
    if (map.solid(start.x, start.y)) {
        return dist;
    }
    std::deque<glm::ivec2> queue{start};
    dist.at(start.x, start.y) = 0;
    constexpr glm::ivec2 kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!queue.empty()) {
        const glm::ivec2 p = queue.front();
        queue.pop_front();
        for (const glm::ivec2 d : kDirs) {
            const glm::ivec2 q = p + d;
            if (!map.tiles.in_bounds(q.x, q.y) || map.solid(q.x, q.y)) continue;
            if (dist.at(q.x, q.y) >= 0) continue;
            dist.at(q.x, q.y) = dist.at(p.x, p.y) + 1;
            queue.push_back(q);
        }
    }
    return dist;
}

std::string render_ascii(const DungeonResult& dungeon) {
    const TileMap& map = dungeon.map;
    std::string out;
    out.reserve(static_cast<size_t>(map.width() + 1) * static_cast<size_t>(map.height()));
    Grid2D<char> canvas(map.width(), map.height(), ' ');
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const Tile t = map.tiles.at(x, y);
            canvas.at(x, y) = t == Tile::Wall ? '#' : (t == Tile::Floor ? '.' : ' ');
        }
    }
    for (const glm::ivec2 m : dungeon.enemy_spawns) {
        canvas.at(m.x, m.y) = 'm';
    }
    canvas.at(dungeon.exit_pos.x, dungeon.exit_pos.y) = '>';
    canvas.at(dungeon.player_spawn.x, dungeon.player_spawn.y) = '@';
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            out.push_back(canvas.at(x, y));
        }
        out.push_back('\n');
    }
    return out;
}

} // namespace ds
