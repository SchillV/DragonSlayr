#include "sim/content.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace ds {

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------
// The generic loader. Adding a content category is now: define the Def struct
// (with an `id` member), write one parse function against JsonReader, and add
// the two ContentDB entry points at the bottom of this file.
//
// Error contract (unit-tested): the first failure wins, messages carry the
// full JSON key path ("enemies.walker.attack.damage: expected a number"), and
// a failed load leaves the previous roster untouched.
// ---------------------------------------------------------------------------

struct ParseCtx {
    std::string* error = nullptr;
    bool ok = true;
};

class JsonReader {
public:
    JsonReader(const json* obj, std::string path, ParseCtx& ctx)
        : obj_(obj), path_(std::move(path)), ctx_(ctx) {}

    void req_f(const char* key, float& out) { number(key, out, /*required=*/true); }
    void opt_f(const char* key, float& out) { number(key, out, /*required=*/false); }
    void req_s(const char* key, std::string& out) { string(key, out, /*required=*/true); }
    void opt_s(const char* key, std::string& out) { string(key, out, /*required=*/false); }

    void opt_i(const char* key, int& out) {
        if (const json* v = find(key, /*required=*/false)) {
            if (!v->is_number_integer()) {
                return fail(key, "expected an integer");
            }
            out = v->get<int>();
        }
    }

    void opt_vec2(const char* key, glm::vec2& out) {
        if (const json* v = find(key, /*required=*/false)) {
            if (!v->is_array() || v->size() != 2 || !(*v)[0].is_number() || !(*v)[1].is_number()) {
                return fail(key, "expected [x, y]");
            }
            out = {(*v)[0].get<float>(), (*v)[1].get<float>()};
        }
    }

    // Maps a string field onto an enum via a name table; unknown names fail
    // with the accepted values listed.
    template <typename E>
    void enum_of(const char* key, E& out,
                 std::initializer_list<std::pair<std::string_view, E>> table,
                 bool required = false) {
        std::string value;
        string(key, value, required);
        if (!ctx_.ok || value.empty()) {
            return;
        }
        std::string names;
        for (const auto& [name, e] : table) {
            if (value == name) {
                out = e;
                return;
            }
            names += names.empty() ? std::string(name) : "|" + std::string(name);
        }
        fail(key, std::format("unknown '{}' ({})", value, names));
    }

    // Nested object; a present-but-wrong-typed key fails, a missing key
    // returns an inert reader (all opts keep their defaults).
    JsonReader sub(const char* key) {
        const json* v = find(key, /*required=*/false);
        if (v && !v->is_object()) {
            fail(key, "expected an object");
            v = nullptr;
        }
        return JsonReader(v, path_ + "." + key, ctx_);
    }

private:
    const json* find(const char* key, bool required) {
        if (!ctx_.ok || !obj_) {
            return nullptr; // inert after the first error / on a missing sub
        }
        const auto it = obj_->find(key);
        if (it == obj_->end()) {
            if (required) {
                fail(key, "required field missing");
            }
            return nullptr;
        }
        return &*it;
    }

    void number(const char* key, float& out, bool required) {
        if (const json* v = find(key, required)) {
            if (!v->is_number()) {
                return fail(key, "expected a number");
            }
            out = v->get<float>();
        }
    }

    void string(const char* key, std::string& out, bool required) {
        if (const json* v = find(key, required)) {
            if (!v->is_string()) {
                return fail(key, "expected a string");
            }
            out = v->get<std::string>();
        }
    }

    void fail(const char* key, std::string_view what) {
        if (ctx_.ok && ctx_.error) {
            *ctx_.error = std::format("{}.{}: {}", path_, key, what);
        }
        ctx_.ok = false;
    }

    const json* obj_;
    std::string path_;
    ParseCtx& ctx_;
};

template <typename Def>
bool load_category(std::string_view json_text, const char* category, std::vector<Def>& out,
                   void (*parse)(JsonReader&, Def&), std::string* error) {
    const json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) {
            *error = std::format("{}: not a valid JSON object", category);
        }
        return false;
    }
    std::vector<Def> parsed;
    for (const auto& [id, value] : doc.items()) {
        const std::string path = std::format("{}.{}", category, id);
        if (!value.is_object()) {
            if (error) {
                *error = std::format("{}: expected an object", path);
            }
            return false;
        }
        Def def;
        def.id = id;
        if constexpr (requires { def.name; }) {
            def.name = id; // display name defaults to the id
        }
        ParseCtx ctx{error};
        JsonReader reader(&value, path, ctx);
        parse(reader, def);
        if (!ctx.ok) {
            return false;
        }
        parsed.push_back(std::move(def));
    }
    out = std::move(parsed);
    return true;
}

bool load_category_file(const std::filesystem::path& path, std::string& out_text,
                        std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) {
            *error = std::format("cannot open {}", path.string());
        }
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out_text = ss.str();
    return true;
}

// ---------------------------------------------------------------------------
// Category parsers.
// ---------------------------------------------------------------------------

void parse_enemy(JsonReader& r, EnemyDef& out) {
    r.opt_s("name", out.name);
    r.req_f("hp", out.hp);
    r.opt_f("speed", out.speed);
    r.opt_f("radius", out.radius);
    r.opt_f("aggro_radius", out.aggro_radius);
    r.req_s("sprite", out.sprite);
    r.opt_vec2("sprite_size", out.sprite_size);
    r.opt_i("score", out.score);
    r.opt_f("spawn_weight", out.spawn_weight);
    r.opt_i("min_floor", out.min_floor);
    r.enum_of("behavior", out.behavior,
              {{"chaser", EnemyBehavior::Chaser},
               {"ranged", EnemyBehavior::Ranged},
               {"charger", EnemyBehavior::Charger},
               {"stationary", EnemyBehavior::Stationary}});
    r.opt_f("keep_distance", out.keep_distance);
    r.opt_f("lunge_speed", out.lunge_speed);

    JsonReader attack = r.sub("attack");
    attack.opt_f("damage", out.attack.damage);
    attack.opt_f("range", out.attack.range);
    attack.opt_f("windup_s", out.attack.windup_s);
    attack.opt_f("cooldown_s", out.attack.cooldown_s);
    attack.opt_f("recovery_s", out.attack.recovery_s);
    attack.opt_f("dodge_window_mult", out.attack.dodge_window_mult);
    attack.opt_f("projectile_speed", out.attack.projectile_speed);
    attack.opt_f("projectile_radius", out.attack.projectile_radius);
}

void parse_weapon(JsonReader& r, WeaponDef& out) {
    r.opt_s("name", out.name);
    r.enum_of("type", out.type,
              {{"melee", WeaponDef::Type::Melee}, {"projectile", WeaponDef::Type::Projectile}},
              /*required=*/true);
    r.req_f("damage", out.damage);
    r.opt_f("cooldown_s", out.cooldown_s);
    r.opt_f("range", out.range);
    r.opt_f("arc_deg", out.arc_deg);
    r.opt_f("swing_s", out.swing_s);
    r.opt_f("speed", out.speed);
    r.opt_f("radius", out.radius);
    r.opt_f("ttl_s", out.ttl_s);
    r.opt_s("sprite", out.sprite);
    r.opt_s("viewmodel", out.viewmodel);
    r.opt_s("sound", out.sound);
}

} // namespace

// ---------------------------------------------------------------------------
// ContentDB entry points.
// ---------------------------------------------------------------------------

int ContentDB::find_enemy(std::string_view id) const {
    return find_by_id(enemies, id);
}

int ContentDB::find_weapon(std::string_view id) const {
    return find_by_id(weapons, id);
}

bool ContentDB::load_enemies_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "enemies", enemies, parse_enemy, error);
}

bool ContentDB::load_enemies(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_enemies_from_string(text, error);
}

bool ContentDB::load_weapons_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "weapons", weapons, parse_weapon, error);
}

bool ContentDB::load_weapons(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_weapons_from_string(text, error);
}

} // namespace ds
