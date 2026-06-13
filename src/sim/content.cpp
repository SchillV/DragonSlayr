#include "sim/content.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace ds {

namespace {

using nlohmann::json;

// Field readers that report "where" on failure: errors carry the full key
// path (e.g. "enemies.walker.attack.damage: expected a number").
bool read_float(const json& obj, const std::string& path, const char* key, float& out,
                std::string* error, bool required = false) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (required && error) {
            *error = std::format("{}.{}: required field missing", path, key);
        }
        return !required;
    }
    if (!it->is_number()) {
        if (error) {
            *error = std::format("{}.{}: expected a number", path, key);
        }
        return false;
    }
    out = it->get<float>();
    return true;
}

bool read_string(const json& obj, const std::string& path, const char* key, std::string& out,
                 std::string* error, bool required = false) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        if (required && error) {
            *error = std::format("{}.{}: required field missing", path, key);
        }
        return !required;
    }
    if (!it->is_string()) {
        if (error) {
            *error = std::format("{}.{}: expected a string", path, key);
        }
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool read_int(const json& obj, const std::string& path, const char* key, int& out,
              std::string* error) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        if (error) {
            *error = std::format("{}.{}: expected an integer", path, key);
        }
        return false;
    }
    out = it->get<int>();
    return true;
}

bool parse_enemy(const std::string& id, const json& obj, EnemyDef& out, std::string* error) {
    const std::string path = "enemies." + id;
    if (!obj.is_object()) {
        if (error) {
            *error = std::format("{}: expected an object", path);
        }
        return false;
    }
    out.id = id;
    out.name = id;
    if (!read_string(obj, path, "name", out.name, error)) return false;
    if (!read_float(obj, path, "hp", out.hp, error, /*required=*/true)) return false;
    if (!read_float(obj, path, "speed", out.speed, error)) return false;
    if (!read_float(obj, path, "radius", out.radius, error)) return false;
    if (!read_float(obj, path, "aggro_radius", out.aggro_radius, error)) return false;
    if (!read_string(obj, path, "sprite", out.sprite, error, /*required=*/true)) return false;
    if (!read_int(obj, path, "score", out.score, error)) return false;
    if (!read_float(obj, path, "spawn_weight", out.spawn_weight, error)) return false;
    if (!read_int(obj, path, "min_floor", out.min_floor, error)) return false;

    if (const auto it = obj.find("sprite_size"); it != obj.end()) {
        if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
            if (error) {
                *error = std::format("{}.sprite_size: expected [w, h]", path);
            }
            return false;
        }
        out.sprite_size = {(*it)[0].get<float>(), (*it)[1].get<float>()};
    }

    if (const auto it = obj.find("attack"); it != obj.end()) {
        const std::string apath = path + ".attack";
        if (!it->is_object()) {
            if (error) {
                *error = std::format("{}: expected an object", apath);
            }
            return false;
        }
        if (!read_float(*it, apath, "damage", out.attack.damage, error)) return false;
        if (!read_float(*it, apath, "range", out.attack.range, error)) return false;
        if (!read_float(*it, apath, "windup_s", out.attack.windup_s, error)) return false;
        if (!read_float(*it, apath, "cooldown_s", out.attack.cooldown_s, error)) return false;
    }
    return true;
}

bool parse_weapon(const std::string& id, const json& obj, WeaponDef& out, std::string* error) {
    const std::string path = "weapons." + id;
    if (!obj.is_object()) {
        if (error) {
            *error = std::format("{}: expected an object", path);
        }
        return false;
    }
    out.id = id;
    out.name = id;
    if (!read_string(obj, path, "name", out.name, error)) return false;

    std::string type;
    if (!read_string(obj, path, "type", type, error, /*required=*/true)) return false;
    if (type == "melee") {
        out.type = WeaponDef::Type::Melee;
    } else if (type == "projectile") {
        out.type = WeaponDef::Type::Projectile;
    } else {
        if (error) {
            *error = std::format("{}.type: must be 'melee' or 'projectile'", path);
        }
        return false;
    }
    if (!read_float(obj, path, "damage", out.damage, error, /*required=*/true)) return false;
    if (!read_float(obj, path, "cooldown_s", out.cooldown_s, error)) return false;
    if (!read_float(obj, path, "range", out.range, error)) return false;
    if (!read_float(obj, path, "arc_deg", out.arc_deg, error)) return false;
    if (!read_float(obj, path, "swing_s", out.swing_s, error)) return false;
    if (!read_float(obj, path, "speed", out.speed, error)) return false;
    if (!read_float(obj, path, "radius", out.radius, error)) return false;
    if (!read_float(obj, path, "ttl_s", out.ttl_s, error)) return false;
    if (!read_string(obj, path, "sprite", out.sprite, error)) return false;
    if (!read_string(obj, path, "viewmodel", out.viewmodel, error)) return false;
    if (!read_string(obj, path, "sound", out.sound, error)) return false;
    return true;
}

} // namespace

int ContentDB::find_enemy(std::string_view id) const {
    for (size_t i = 0; i < enemies.size(); ++i) {
        if (enemies[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ContentDB::find_weapon(std::string_view id) const {
    for (size_t i = 0; i < weapons.size(); ++i) {
        if (weapons[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool ContentDB::load_enemies_from_string(std::string_view json_text, std::string* error) {
    const json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) {
            *error = "enemies: not a valid JSON object";
        }
        return false;
    }
    std::vector<EnemyDef> parsed;
    for (const auto& [id, value] : doc.items()) {
        EnemyDef def;
        if (!parse_enemy(id, value, def, error)) {
            return false;
        }
        parsed.push_back(std::move(def));
    }
    enemies = std::move(parsed);
    return true;
}

bool ContentDB::load_enemies(const std::filesystem::path& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) {
            *error = std::format("cannot open {}", path.string());
        }
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return load_enemies_from_string(ss.str(), error);
}

bool ContentDB::load_weapons_from_string(std::string_view json_text, std::string* error) {
    const json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) {
            *error = "weapons: not a valid JSON object";
        }
        return false;
    }
    std::vector<WeaponDef> parsed;
    for (const auto& [id, value] : doc.items()) {
        WeaponDef def;
        if (!parse_weapon(id, value, def, error)) {
            return false;
        }
        parsed.push_back(std::move(def));
    }
    weapons = std::move(parsed);
    return true;
}

bool ContentDB::load_weapons(const std::filesystem::path& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) {
            *error = std::format("cannot open {}", path.string());
        }
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return load_weapons_from_string(ss.str(), error);
}

} // namespace ds
