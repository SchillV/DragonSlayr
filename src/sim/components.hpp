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
    // combat
    float primary_cooldown = 0.0f;   // sword
    float secondary_cooldown = 0.0f; // bolt
    float swing_anim = 0.0f;         // 1 -> 0 over the swing, drives the viewmodel
    float cast_anim = 0.0f;          // 1 -> 0 after firing a bolt
    float hurt_flash = 0.0f;         // 1 -> 0, drives the HUD red flash
};

// Whose side a projectile is on — decides what it can hit.
enum class Team : uint8_t { Player, Enemy };

struct Projectile {
    Team team = Team::Player;
    uint16_t weapon = 0xffff; // originating weapon def (player shots), 0xffff if none
    uint16_t src_def = 0xffff; // originating enemy def (enemy shots), for player-damage telemetry
    float damage = 5.0f;
    float radius = 0.1f;
    float ttl = 3.0f;
};

// Brief white/red blink on enemies when damaged; drives the sprite flash.
struct HurtFlash {
    float t = 0.0f; // 1 -> 0
};

enum class AiState : uint8_t { Idle, Chase, Windup, Recover, Lunge };

struct Enemy {
    uint16_t def = 0; // index into ContentDB::enemies
    AiState state = AiState::Idle;
    float state_time = 0.0f;
    float repath_timer = 0.0f;
    float attack_cooldown = 0.0f;
    uint32_t spawn_tick = 0; // for kill telemetry (alive seconds)
    glm::vec2 lunge_dir{0.0f}; // charger: locked-in dash direction
    std::vector<glm::vec2> path; // smoothed waypoints, world space
    size_t path_index = 0;
};

} // namespace ds
