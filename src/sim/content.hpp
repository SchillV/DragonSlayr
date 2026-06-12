#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ds {

struct EnemyAttackDef {
    float damage = 6.0f;
    float range = 0.9f;
    float windup_s = 0.35f;
    float cooldown_s = 0.8f;
};

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
    EnemyAttackDef attack;
};

// All data-driven definitions. String ids are interned to indices at load;
// live entities hold indices, which is what makes hot reload instant.
struct ContentDB {
    std::vector<EnemyDef> enemies;

    int find_enemy(std::string_view id) const;

    // On failure: returns false, fills `error` (with the offending JSON key
    // path) and leaves the db unchanged.
    bool load_enemies_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_enemies(const std::filesystem::path& path, std::string* error = nullptr);
};

} // namespace ds
