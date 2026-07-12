#pragma once

#include "sim/stats.hpp"

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

// A passive stat bonus an item grants while held.
struct ItemModifierDef {
    StatId stat = StatId::MaxHp;
    Modifier::Op op = Modifier::Op::Add;
    float value = 0.0f;
};

// A reactive effect: when `on` fires, run `effect` with these parameters.
// Both palettes are curated C++ (hybrid philosophy); items combine them.
struct ItemHookDef {
    enum class Trigger : uint8_t { OnHit, OnKill, OnDamaged, OnPickup };
    enum class Effect : uint8_t { Heal, TempStat, AoeDamage };

    Trigger on = Trigger::OnKill;
    Effect effect = Effect::Heal;
    float amount = 0.0f;     // heal hp / aoe damage
    float radius = 2.0f;     // aoe_damage
    float duration_s = 3.0f; // temp_stat
    StatId stat = StatId::MaxHp;
    Modifier::Op op = Modifier::Op::Add;
    float value = 0.0f; // temp_stat modifier value
};

struct ItemDef {
    std::string id;   // JSON object key
    std::string name; // display name (defaults to id)
    std::string sprite;
    glm::vec2 sprite_size{0.5f, 0.5f};
    float spawn_weight = 1.0f; // relative frequency in item spots
    int min_floor = 1;
    std::vector<ItemModifierDef> modifiers;
    std::vector<ItemHookDef> hooks;
};

// A feat is the item shape minus the world pickup: passive modifiers plus
// reactive hooks, granted by classes, skill trees, or the console. Feats can
// stack ("BLOODLUST x3"): modifiers apply per stack and hooks fire per stack.
struct FeatDef {
    std::string id;
    std::string name;
    std::string desc;   // one-liner for menus/tooltips
    std::string sprite; // optional chip icon
    int max_stacks = 1; // 0 = unlimited
    std::vector<ItemModifierDef> modifiers;
    std::vector<ItemHookDef> hooks;
};

// A playable class: starting attributes, starting feats, weapon loadout.
// Applied once at run start (attributes as kClassSource modifiers, feats via
// grant_feat) — after that the run belongs to items/trees, so classes stay
// cheap: a class IS its starting numbers plus its feat collection.
struct ClassDef {
    std::string id;
    std::string name;
    std::string desc;
    int str = 0;
    int dex = 0;
    int vit = 0;
    int mag = 0;
    std::vector<std::string> feats; // feat ids granted at run start
    std::string primary = "sword";
    std::string secondary = "bolt";
};

// Linear id lookup shared by every content category (rosters are small; a
// hash map would be overkill and would hurt hot-reload index stability).
template <typename Def>
int find_by_id(const std::vector<Def>& defs, std::string_view id) {
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// All data-driven definitions. String ids are interned to indices at load;
// live entities hold indices, which is what makes hot reload instant.
struct ContentDB {
    std::vector<EnemyDef> enemies;
    std::vector<WeaponDef> weapons;
    std::vector<ItemDef> items;
    std::vector<FeatDef> feats;
    std::vector<ClassDef> classes;

    int find_enemy(std::string_view id) const;
    int find_weapon(std::string_view id) const;
    int find_item(std::string_view id) const;
    int find_feat(std::string_view id) const;
    int find_class(std::string_view id) const;

    // On failure: returns false, fills `error` (with the offending JSON key
    // path) and leaves the db unchanged.
    bool load_enemies_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_enemies(const std::filesystem::path& path, std::string* error = nullptr);
    bool load_weapons_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_weapons(const std::filesystem::path& path, std::string* error = nullptr);
    bool load_items_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_items(const std::filesystem::path& path, std::string* error = nullptr);
    bool load_feats_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_feats(const std::filesystem::path& path, std::string* error = nullptr);
    bool load_classes_from_string(std::string_view json_text, std::string* error = nullptr);
    bool load_classes(const std::filesystem::path& path, std::string* error = nullptr);
};

} // namespace ds
