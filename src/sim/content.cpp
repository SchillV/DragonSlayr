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

    // Visits each element of an optional array of objects with its own
    // indexed reader ("path.key[2]" in error messages).
    template <typename Fn>
    void arr(const char* key, Fn&& fn) {
        const json* v = find(key, /*required=*/false);
        if (!v) {
            return;
        }
        if (!v->is_array()) {
            return fail(key, "expected an array");
        }
        for (size_t i = 0; i < v->size(); ++i) {
            std::string epath = std::format("{}.{}[{}]", path_, key, i);
            if (!(*v)[i].is_object()) {
                if (ctx_.ok && ctx_.error) {
                    *ctx_.error = epath + ": expected an object";
                }
                ctx_.ok = false;
                return;
            }
            JsonReader elem(&(*v)[i], std::move(epath), ctx_);
            fn(elem);
            if (!ctx_.ok) {
                return;
            }
        }
    }

    // Visits each entry of an optional id-keyed object (a nested roster, e.g.
    // skill tree nodes) with its own reader ("path.key.entry_id" in errors).
    template <typename Fn>
    void obj_items(const char* key, Fn&& fn) {
        const json* v = find(key, /*required=*/false);
        if (!v) {
            return;
        }
        if (!v->is_object()) {
            return fail(key, "expected an object");
        }
        for (const auto& [id, value] : v->items()) {
            std::string epath = std::format("{}.{}.{}", path_, key, id);
            if (!value.is_object()) {
                if (ctx_.ok && ctx_.error) {
                    *ctx_.error = epath + ": expected an object";
                }
                ctx_.ok = false;
                return;
            }
            JsonReader entry(&value, std::move(epath), ctx_);
            fn(id, entry);
            if (!ctx_.ok) {
                return;
            }
        }
    }

    void opt_s_array(const char* key, std::vector<std::string>& out) {
        const json* v = find(key, /*required=*/false);
        if (!v) {
            return;
        }
        if (!v->is_array()) {
            return fail(key, "expected an array of strings");
        }
        for (const auto& e : *v) {
            if (!e.is_string()) {
                return fail(key, "expected an array of strings");
            }
            out.push_back(e.get<std::string>());
        }
    }

    // For custom validation in parse functions (e.g. stat name lookups).
    void error_at(const char* key, std::string_view what) { fail(key, what); }

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
    r.opt_i("xp", out.xp);
    if (out.xp < 0) {
        out.xp = std::max(1, out.score / 5); // sane default: xp tracks score
    }
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

void parse_stat_ref(JsonReader& r, StatId& stat, Modifier::Op& op) {
    std::string name;
    r.req_s("stat", name);
    if (!name.empty() && !stat_from_name(name, stat)) {
        r.error_at("stat", std::format("unknown stat '{}'", name));
    }
    r.enum_of("op", op, {{"add", Modifier::Op::Add}, {"mult", Modifier::Op::Mult}});
}

// Modifier/hook lists are shared vocabulary between items and feats.
void parse_modifier_list(JsonReader& r, std::vector<ItemModifierDef>& out) {
    r.arr("modifiers", [&out](JsonReader& m) {
        ItemModifierDef def;
        parse_stat_ref(m, def.stat, def.op);
        m.req_f("value", def.value);
        out.push_back(def);
    });
}

void parse_hook_list(JsonReader& r, std::vector<ItemHookDef>& out) {
    r.arr("hooks", [&out](JsonReader& h) {
        ItemHookDef def;
        h.enum_of("on", def.on,
                  {{"on_hit", ItemHookDef::Trigger::OnHit},
                   {"on_kill", ItemHookDef::Trigger::OnKill},
                   {"on_damaged", ItemHookDef::Trigger::OnDamaged},
                   {"on_pickup", ItemHookDef::Trigger::OnPickup}},
                  /*required=*/true);
        h.enum_of("effect", def.effect,
                  {{"heal", ItemHookDef::Effect::Heal},
                   {"temp_stat", ItemHookDef::Effect::TempStat},
                   {"aoe_damage", ItemHookDef::Effect::AoeDamage}},
                  /*required=*/true);
        h.opt_f("amount", def.amount);
        h.opt_f("radius", def.radius);
        h.opt_f("duration_s", def.duration_s);
        if (def.effect == ItemHookDef::Effect::TempStat) {
            parse_stat_ref(h, def.stat, def.op);
            h.req_f("value", def.value);
        }
        out.push_back(def);
    });
}

void parse_item(JsonReader& r, ItemDef& out) {
    r.opt_s("name", out.name);
    r.req_s("sprite", out.sprite);
    r.opt_vec2("sprite_size", out.sprite_size);
    r.opt_f("spawn_weight", out.spawn_weight);
    r.opt_i("min_floor", out.min_floor);
    parse_modifier_list(r, out.modifiers);
    parse_hook_list(r, out.hooks);
}

void parse_feat(JsonReader& r, FeatDef& out) {
    r.opt_s("name", out.name);
    r.opt_s("desc", out.desc);
    r.opt_s("sprite", out.sprite);
    r.opt_i("max_stacks", out.max_stacks);
    parse_modifier_list(r, out.modifiers);
    parse_hook_list(r, out.hooks);
}

void parse_class(JsonReader& r, ClassDef& out) {
    r.opt_s("name", out.name);
    r.opt_s("desc", out.desc);
    r.opt_i("str", out.str);
    r.opt_i("dex", out.dex);
    r.opt_i("vit", out.vit);
    r.opt_i("mag", out.mag);
    r.opt_s_array("feats", out.feats);
    r.opt_s("primary", out.primary);
    r.opt_s("secondary", out.secondary);
    r.opt_s("tree", out.tree);
}

void parse_boss(JsonReader& r, BossDef& out) {
    r.opt_s("name", out.name);
    r.req_s("sprite", out.sprite);
    r.opt_vec2("sprite_size", out.sprite_size);
    r.req_f("hp", out.hp);
    r.opt_f("hp_per_floor", out.hp_per_floor);
    r.opt_f("speed", out.speed);
    r.opt_f("radius", out.radius);
    r.opt_f("aggro_radius", out.aggro_radius);
    r.opt_f("contact_damage", out.contact_damage);
    r.opt_f("phase2_at", out.phase2_at);
    r.opt_i("score", out.score);
    r.opt_i("xp", out.xp);
    r.opt_f("spawn_weight", out.spawn_weight);
    r.opt_i("min_floor", out.min_floor);
    r.arr("patterns", [&out](JsonReader& p) {
        BossPatternDef def;
        p.enum_of("pattern", def.pattern,
                  {{"ground_slam", BossPattern::GroundSlam},
                   {"summon_adds", BossPattern::SummonAdds},
                   {"charge", BossPattern::Charge},
                   {"projectile_ring", BossPattern::ProjectileRing}},
                  /*required=*/true);
        p.opt_f("weight", def.weight);
        p.opt_f("cooldown_s", def.cooldown_s);
        p.opt_f("damage", def.damage);
        p.opt_f("radius", def.radius);
        p.opt_i("count", def.count);
        p.opt_f("speed", def.speed);
        out.patterns.push_back(def);
    });
}

void parse_upgrade(JsonReader& r, UpgradeDef& out) {
    r.opt_s("name", out.name);
    r.opt_s("desc", out.desc);
    r.opt_i("max_ranks", out.max_ranks);
    r.opt_i("cost", out.cost);
    r.opt_i("cost_per_rank", out.cost_per_rank);
    parse_modifier_list(r, out.modifiers);
}

void parse_skill_tree(JsonReader& r, SkillTreeDef& out) {
    r.opt_s("name", out.name);
    r.obj_items("nodes", [&out](const std::string& id, JsonReader& n) {
        SkillNodeDef node;
        node.id = id;
        node.name = id;
        n.opt_s("name", node.name);
        n.opt_s("feat", node.feat);
        std::string attr_name;
        n.opt_s("attr", attr_name);
        if (!attr_name.empty() && !stat_from_name(attr_name, node.attr)) {
            return n.error_at("attr", std::format("unknown stat '{}'", attr_name));
        }
        n.opt_i("points", node.attr_points);
        n.opt_i("cost", node.cost);
        n.opt_s_array("requires", node.prereqs);
        if (node.feat.empty() == (node.attr == StatId::Count)) {
            return n.error_at("feat", "a node grants exactly one of 'feat' or 'attr'");
        }
        out.nodes.push_back(std::move(node));
    });
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

int ContentDB::find_item(std::string_view id) const {
    return find_by_id(items, id);
}

int ContentDB::find_feat(std::string_view id) const {
    return find_by_id(feats, id);
}

int ContentDB::find_class(std::string_view id) const {
    return find_by_id(classes, id);
}

int ContentDB::find_skill_tree(std::string_view id) const {
    return find_by_id(skill_trees, id);
}

int ContentDB::find_upgrade(std::string_view id) const {
    return find_by_id(upgrades, id);
}

int ContentDB::find_boss(std::string_view id) const {
    return find_by_id(bosses, id);
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

bool ContentDB::load_items_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "items", items, parse_item, error);
}

bool ContentDB::load_items(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_items_from_string(text, error);
}

bool ContentDB::load_feats_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "feats", feats, parse_feat, error);
}

bool ContentDB::load_feats(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_feats_from_string(text, error);
}

bool ContentDB::load_classes_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "classes", classes, parse_class, error);
}

bool ContentDB::load_classes(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_classes_from_string(text, error);
}

bool ContentDB::load_skill_trees_from_string(std::string_view json_text, std::string* error) {
    std::vector<SkillTreeDef> parsed;
    if (!load_category(json_text, "skill_trees", parsed, parse_skill_tree, error)) {
        return false;
    }
    // Cross-node validation the per-node parser can't do: prereq ids resolve,
    // feat references exist, and the graph is acyclic.
    for (SkillTreeDef& tree : parsed) {
        for (SkillNodeDef& node : tree.nodes) {
            if (!node.feat.empty() && find_feat(node.feat) < 0) {
                if (error) {
                    *error = std::format("skill_trees.{}.nodes.{}: unknown feat '{}'", tree.id,
                                         node.id, node.feat);
                }
                return false;
            }
            for (const std::string& req : node.prereqs) {
                const int idx = tree.find_node(req);
                if (idx < 0) {
                    if (error) {
                        *error = std::format("skill_trees.{}.nodes.{}: requires unknown node '{}'",
                                             tree.id, node.id, req);
                    }
                    return false;
                }
                node.prereq_idx.push_back(idx);
            }
        }
        // Kahn's algorithm: if not every node drains, there's a cycle.
        std::vector<int> indegree(tree.nodes.size(), 0);
        for (const SkillNodeDef& node : tree.nodes) {
            indegree[static_cast<size_t>(tree.find_node(node.id))] =
                static_cast<int>(node.prereq_idx.size());
        }
        std::vector<int> frontier;
        for (size_t i = 0; i < tree.nodes.size(); ++i) {
            if (indegree[i] == 0) {
                frontier.push_back(static_cast<int>(i));
            }
        }
        size_t drained = 0;
        while (!frontier.empty()) {
            const int n = frontier.back();
            frontier.pop_back();
            ++drained;
            for (size_t i = 0; i < tree.nodes.size(); ++i) {
                for (const int p : tree.nodes[i].prereq_idx) {
                    if (p == n && --indegree[i] == 0) {
                        frontier.push_back(static_cast<int>(i));
                    }
                }
            }
        }
        if (drained != tree.nodes.size()) {
            if (error) {
                *error = std::format("skill_trees.{}: 'requires' edges form a cycle", tree.id);
            }
            return false;
        }
    }
    skill_trees = std::move(parsed);
    return true;
}

bool ContentDB::load_skill_trees(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_skill_trees_from_string(text, error);
}

bool ContentDB::load_upgrades_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "upgrades", upgrades, parse_upgrade, error);
}

bool ContentDB::load_upgrades(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_upgrades_from_string(text, error);
}

bool ContentDB::load_bosses_from_string(std::string_view json_text, std::string* error) {
    return load_category(json_text, "bosses", bosses, parse_boss, error);
}

bool ContentDB::load_bosses(const std::filesystem::path& path, std::string* error) {
    std::string text;
    return load_category_file(path, text, error) && load_bosses_from_string(text, error);
}

} // namespace ds
