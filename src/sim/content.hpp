#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ds {

struct EnemyAttackDef {
    float damage = 6.0f;
    float range = 0.9f;          // melee reach, or max firing distance for ranged
    float windup_s = 0.35f;      // telegraph before the hit/shot lands
    float cooldown_s = 0.8f;
    float recovery_s = 0.25f;    // pause after attacking before re-engaging
    float dodge_window_mult = 1.3f; // melee connects if target within range*this at strike
    float projectile_speed = 10.0f;  // ranged
    float projectile_radius = 0.18f; // ranged
};

// Curated AI palette (the hybrid seam): data picks one, C++ implements it.
enum class EnemyBehavior : uint8_t { Chaser, Ranged, Charger, Stationary };

struct EnemyDef {
    std::string id; // JSON object key
    std::string name;
    float hp = 12.0f;
    float speed = 3.0f;
    float radius = 0.35f;
    float aggro_radius = 10.0f;
    std::string sprite;             // texture name in assets/textures (no extension)
    glm::vec2 sprite_size{0.9f, 0.9f};
    int score = 50;
    float spawn_weight = 1.0f; // relative weight in spawn selection (0 = never auto-spawns)
    int min_floor = 1;         // earliest floor this enemy may appear on
    EnemyBehavior behavior = EnemyBehavior::Chaser;
    float keep_distance = 6.0f; // ranged: preferred distance to maintain from the player
    float lunge_speed = 16.0f;  // charger: dash speed during the lunge
    EnemyAttackDef attack;
};

struct WeaponDef {
    enum class Type { Melee, Projectile };

    std::string id; // JSON object key
    std::string name;
    Type type = Type::Melee;
    float damage = 5.0f;
    float cooldown_s = 0.5f;
    // melee
    float range = 1.6f;
    float arc_deg = 75.0f;
    float swing_s = 0.28f;
    // projectile
    float speed = 14.0f;
    float radius = 0.1f;
    float ttl_s = 3.0f;
    std::string sprite;    // projectile billboard texture
    std::string viewmodel; // overlay texture for the first-person hands
    std::string sound;
};

// All data-driven definitions. String ids are interned to indices at load;
// live entities hold indices, which is what makes hot reload instant.
struct ContentDB {
    std::vector<EnemyDef> enemies;
    std::vector<WeaponDef> weapons;

    int find_enemy(std::string_view id) const;
    int find_weapon(std::string_view id) const;

    // On failure: returns false, fills `error` (with the offending JSON key
    // path) and leaves the db unchanged.
    bool load_enemies_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_enemies(const std::filesystem::path& path, std::string* error = nullptr);
    bool load_weapons_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_weapons(const std::filesystem::path& path, std::string* error = nullptr);
};

} // namespace ds
