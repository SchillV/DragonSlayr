#pragma once

#include <glm/glm.hpp>

namespace ds {

// The sim is 2D on the tile plane; rendering adds eye height / sprite size.
struct Transform {
    glm::vec2 pos{0.0f};
    float yaw = 0.0f;
};

// Snapshot of Transform at the start of the tick; render interpolates between
// the two. Copied as system #0 every tick.
struct PrevTransform {
    glm::vec2 pos{0.0f};
    float yaw = 0.0f;
};

struct Velocity {
    glm::vec2 v{0.0f};
};

struct Body {
    float radius = 0.3f;
};

struct Health {
    float hp = 100.0f;
    float max_hp = 100.0f;
};

struct Player {
    float dash_cooldown = 0.0f;
    float dash_time_left = 0.0f;
    glm::vec2 dash_dir{0.0f};
};

} // namespace ds
