#pragma once

#include <glm/glm.hpp>

namespace ds {

struct World;

// The only thing the sim ever learns about input. Headless bots and (later)
// demo replays produce these too — that is the point of the indirection.
struct PlayerCmd {
    float yaw = 0.0f;
    float pitch = 0.0f;
    glm::vec2 wish_dir{0.0f}; // world-space, normalized (or zero)
    bool run = true;          // boomer default: running IS the base speed
    bool dash = false;        // edge
    bool attack_primary = false;
    bool attack_secondary = false;
    bool interact = false;    // edge
};

void player_apply_cmd(World& world, const PlayerCmd& cmd, float dt);

} // namespace ds
