#pragma once

#include "sim/player.hpp"

#include <string>

namespace ds {

struct World;

// Deterministic command source for headless smoke tests: wanders the dungeon,
// turning when a wall comes up. "walk_attack" additionally swings/fires (M5).
class Bot {
public:
    explicit Bot(std::string mode) : mode_(std::move(mode)) {}

    bool active() const { return !mode_.empty(); }
    PlayerCmd make_cmd(World& world);

private:
    std::string mode_;
    float yaw_ = 0.0f;
    int ticks_ = 0;
};

} // namespace ds
