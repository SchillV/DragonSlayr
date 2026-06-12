#include "sim/tilemap.hpp"

namespace ds {

uint64_t TileMap::hash() const {
    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t h = kOffset;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= kPrime;
        }
    };
    mix(static_cast<uint64_t>(width()));
    mix(static_cast<uint64_t>(height()));
    for (const Tile t : tiles.cells()) {
        h ^= static_cast<uint64_t>(t);
        h *= kPrime;
    }
    return h;
}

} // namespace ds
