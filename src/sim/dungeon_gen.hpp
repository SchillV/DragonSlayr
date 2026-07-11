#pragma once

#include "sim/tilemap.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ds {

struct GenParams {
    uint64_t seed = 1;
    int width = 64;
    int height = 64;
    int rooms_min = 9;
    int rooms_max = 14;
    int room_min = 5;  // interior side length
    int room_max = 12;
    float loop_edges = 0.15f; // fraction of shortest non-tree edges re-added as loops
    int max_place_attempts = 240;
    int floor = 1; // scales enemy budgets; gates min_floor spawn tables downstream
};

// Room roles are derived from pure geometry (largest room = arena, smallest =
// treasure vault) so typing never disturbs the RNG sequence the golden-hash
// test pins down.
enum class RoomType : uint8_t { Normal, Arena, Treasure };

struct Room {
    int x = 0, y = 0, w = 0, h = 0; // interior rect (floor tiles)
    RoomType type = RoomType::Normal;
    glm::ivec2 center() const { return {x + w / 2, y + h / 2}; }
    int area() const { return w * h; }
    bool contains(glm::ivec2 p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

struct DungeonResult {
    TileMap map;
    std::vector<Room> rooms;
    glm::ivec2 player_spawn{1, 1};
    glm::ivec2 exit_pos{1, 1};                // the stairs down (functional since the floors milestone)
    std::vector<glm::ivec2> enemy_spawns;
    std::vector<glm::ivec2> item_spawns;
};

// Pure function of params: rooms via rejection placement, Prim MST over centers,
// a fraction of short non-tree edges re-added for loops, L-shaped corridors.
// Determinism contract: ONE Rng stream, no unordered containers, stable order.
DungeonResult generate_dungeon(const GenParams& params);

// BFS over floor tiles; -1 = unreachable. Used for exit placement and tests.
Grid2D<int> bfs_distances(const TileMap& map, glm::ivec2 start);

// '#' wall, '.' floor, ' ' void, '@' spawn, '>' exit, 'm' enemy spawn.
std::string render_ascii(const DungeonResult& dungeon);

} // namespace ds
