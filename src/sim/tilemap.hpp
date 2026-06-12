#pragma once

#include "core/grid.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace ds {

// Void = outside the dungeon (solid, never rendered). Wall = solid, rendered.
// Floor = walkable.
enum class Tile : uint8_t { Void = 0, Floor = 1, Wall = 2 };

struct TileMap {
    Grid2D<Tile> tiles;

    int width() const { return tiles.width(); }
    int height() const { return tiles.height(); }

    bool solid(int x, int y) const { return tiles.get_or(x, y, Tile::Void) != Tile::Floor; }
    bool solid_at(glm::vec2 p) const {
        return solid(static_cast<int>(glm::floor(p.x)), static_cast<int>(glm::floor(p.y)));
    }

    // FNV-1a over dimensions + cells; the determinism golden tests rely on it.
    uint64_t hash() const;
};

} // namespace ds
