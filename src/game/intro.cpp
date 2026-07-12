#include "game/intro.hpp"

namespace ds {

DungeonResult intro_approach_map() {
    // A 34x13 canyon: a winding 3-wide path west→east, widening into a small
    // clearing where the guards stand, then narrowing into the cave mouth.
    constexpr int kW = 34;
    constexpr int kH = 13;
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(kW, kH, Tile::Wall);

    auto carve = [&d](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                d.map.tiles.at(x, y) = Tile::Floor;
            }
        }
    };
    carve(1, 5, 9, 7);    // western approach
    carve(9, 3, 13, 9);   // first bend opens north/south
    carve(13, 4, 21, 8);  // the guards' clearing
    carve(21, 5, 28, 7);  // narrowing path
    carve(28, 6, 32, 6);  // the throat: single-wide into the dark

    d.rooms.push_back({13, 4, 9, 5}); // the clearing, for completeness
    d.player_spawn = {2, 6};
    d.exit_pos = {32, 6}; // the cave mouth — stepping here binds the hero
    d.enemy_spawns.push_back({16, 6});
    d.enemy_spawns.push_back({19, 5});
    return d;
}

} // namespace ds
