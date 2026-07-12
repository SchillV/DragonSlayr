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

// Marked for destruction at the end of the tick (cleanup_dead) — never
// destroy entities mid-system while views are being iterated.
struct Doomed {};

// A floor item waiting to be walked over.
struct Pickup {
    uint16_t item = 0;      // index into ContentDB::items
    float bob_phase = 0.0f; // render-only idle wobble offset
};

// The player's held items (ContentDB::items indices, in pickup order).
struct Inventory {
    std::vector<uint16_t> items;
};

// Active feats with stack counts (ContentDB::feats indices). Modifiers apply
// per stack and hooks fire per stack.
struct FeatSet {
    struct Entry {
        uint16_t feat = 0;
        uint8_t count = 0;
    };
    std::vector<Entry> entries;
};

// Live temp_stat effects. Each entry owns a unique source token in the
// reserved kTempSourceBase+ range so expiry removes exactly its modifier.
struct TempMods {
    struct Entry {
        uint16_t source = 0;
        float ttl = 0.0f;
    };
    std::vector<Entry> entries;
    uint16_t next_token = 0; // wraps within the reserved range
};

// The floor boss. Bosses are not Enemy entities: they steer straight at the
// player in their open arena and act through data-weighted patterns instead
// of the shared chase AI.
struct Boss {
    uint16_t def = 0; // index into ContentDB::bosses
    int phase = 1;
    bool engaged = false;
    float pattern_cooldown = 1.6f; // time until the next pattern pick
    uint8_t active_pattern = 0xff; // index into def.patterns, 0xff = none
    float pattern_time = 0.0f;     // elapsed inside the active pattern
    glm::vec2 charge_dir{0.0f};
    glm::vec2 slam_pos{0.0f};
    float contact_cooldown = 0.0f;
    // fight-summary accumulators (telemetry)
    uint32_t engage_tick = 0;
    float player_hp_at_engage = 0.0f;
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
