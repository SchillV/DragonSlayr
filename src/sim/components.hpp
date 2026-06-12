#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

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

enum class AiState : uint8_t { Idle, Chase, Windup, Recover };

struct Enemy {
    uint16_t def = 0; // index into ContentDB::enemies
    AiState state = AiState::Idle;
    float state_time = 0.0f;
    float repath_timer = 0.0f;
    float attack_cooldown = 0.0f;
    std::vector<glm::vec2> path; // smoothed waypoints, world space
    size_t path_index = 0;
};

} // namespace ds
