#include "sim/world.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "sim/collision.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/enemy_ai.hpp"
#include "sim/items.hpp"
#include "sim/stats.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ds {

namespace {

CVar& sv_noclip = cvar_register("sv.noclip", 0.0f, "player ignores walls", CVAR_CHEAT);

void copy_prev_transforms(entt::registry& reg) {
    for (auto [e, tr, prev] : reg.view<Transform, PrevTransform>().each()) {
        prev.pos = tr.pos;
        prev.yaw = tr.yaw;
    }
}

void move_and_collide(World& world, float dt) {
    for (auto [e, tr, vel, body] : world.reg.view<Transform, Velocity, Body>().each()) {
        const glm::vec2 delta = vel.v * dt;
        if (e == world.player && sv_noclip.as_bool()) {
            tr.pos += delta;
            continue;
        }
        const MoveResult res = slide_move(world.map(), tr.pos, body.radius, delta);
        tr.pos = res.pos;
        if (res.hit_x) vel.v.x = 0.0f;
        if (res.hit_y) vel.v.y = 0.0f;
    }
}

} // namespace

void World::init_from_dungeon(DungeonResult d, uint64_t s) {
    seed = s;
    tick_count = 0;
    score = 0;
    current_floor = 1;
    cvar_reset_cheat_touched();

    telem.begin_run(s);
    TelemetryEvent start;
    start.type = EvType::RunStart;
    telem.record(start);

    setup_floor(std::move(d));
}

void World::advance_floor(DungeonResult d) {
    Health carried_hp = reg.get<Health>(player);
    StatBlock carried_stats = std::move(reg.get<StatBlock>(player));
    Inventory carried_inv;
    if (auto* inv = reg.try_get<Inventory>(player)) {
        carried_inv = std::move(*inv);
    }
    FeatSet carried_feats;
    if (auto* feats = reg.try_get<FeatSet>(player)) {
        carried_feats = std::move(*feats);
    }
    // Temp buffs die at the stairs (their TempMods bookkeeping is per-floor).
    std::erase_if(carried_stats.mods,
                  [](const Modifier& m) { return m.source >= kTempSourceBase; });

    ++current_floor;
    setup_floor(std::move(d));

    reg.emplace_or_replace<Inventory>(player, std::move(carried_inv));
    reg.emplace_or_replace<FeatSet>(player, std::move(carried_feats));
    auto& stats = reg.get<StatBlock>(player);
    stats = std::move(carried_stats);
    stats.recompute();
    auto& hp = reg.get<Health>(player);
    hp.max_hp = stats.cached.max_hp;
    hp.hp = std::clamp(carried_hp.hp, 1.0f, hp.max_hp); // arrive alive

    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(tick_count);
    ev.type = EvType::FloorAdvance;
    ev.a = static_cast<float>(current_floor);
    telem.record(ev);
    log_info("descended to floor {} (score {})", current_floor, score);
}

void World::setup_floor(DungeonResult d) {
    reg.clear();
    dungeon = std::move(d);
    // Distinct stream from generation, folded with the floor so each depth
    // draws different spawns (floor 1 matches the pre-floors stream).
    rng = Rng(seed ^ (0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(current_floor)));
    player_dead = false;
    floor_exit_requested = false;

    primary_weapon = content.find_weapon("sword");
    secondary_weapon = content.find_weapon("bolt");

    player = reg.create();
    const glm::vec2 spawn{static_cast<float>(dungeon.player_spawn.x) + 0.5f,
                          static_cast<float>(dungeon.player_spawn.y) + 0.5f};
    reg.emplace<Transform>(player, spawn, 0.0f);
    reg.emplace<PrevTransform>(player, spawn, 0.0f);
    reg.emplace<Velocity>(player);
    reg.emplace<Body>(player, 0.3f);
    // All player numbers flow through the stat block (base + item/class
    // modifiers -> cached); Health seeds from it instead of a hardcoded 100.
    const StatBlock& stats = reg.emplace<StatBlock>(player);
    reg.emplace<Health>(player, stats.cached.max_hp, stats.cached.max_hp);
    reg.emplace<Player>(player);

    // Weighted spawn table: each spawn point draws an eligible enemy by
    // spawn_weight. Adding an enemy to enemies.json with spawn_weight > 0 is
    // all it takes to have it appear.
    bool spawned_any = false;
    for (const glm::ivec2 sp : dungeon.enemy_spawns) {
        const int def_index = pick_enemy_for_floor(content, current_floor, rng);
        if (def_index < 0) {
            break; // nothing eligible on this floor
        }
        spawn_enemy(def_index, {static_cast<float>(sp.x) + 0.5f, static_cast<float>(sp.y) + 0.5f});
        spawned_any = true;
    }
    if (!spawned_any && !content.enemies.empty() && !dungeon.enemy_spawns.empty()) {
        log_warn("no enemy is eligible to spawn on floor {} (check spawn_weight/min_floor)",
                 current_floor);
    }

    // Item spots draw from the same weighted-table mechanism as enemies.
    for (const glm::ivec2 sp : dungeon.item_spawns) {
        const int item_index = pick_item_for_floor(content, current_floor, rng);
        if (item_index < 0) {
            break;
        }
        spawn_pickup(item_index, {static_cast<float>(sp.x) + 0.5f, static_cast<float>(sp.y) + 0.5f});
    }
}

entt::entity World::spawn_enemy(int def_index, glm::vec2 pos) {
    const EnemyDef& def = content.enemies[static_cast<size_t>(def_index)];
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, pos, 0.0f);
    reg.emplace<PrevTransform>(e, pos, 0.0f);
    reg.emplace<Velocity>(e);
    reg.emplace<Body>(e, def.radius);
    reg.emplace<Health>(e, def.hp, def.hp);
    Enemy enemy;
    enemy.def = static_cast<uint16_t>(def_index);
    enemy.spawn_tick = static_cast<uint32_t>(tick_count);
    reg.emplace<Enemy>(e, std::move(enemy));
    return e;
}

entt::entity World::spawn_pickup(int item_index, glm::vec2 pos) {
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, pos, 0.0f);
    reg.emplace<PrevTransform>(e, pos, 0.0f);
    Pickup pk;
    pk.item = static_cast<uint16_t>(item_index);
    pk.bob_phase = rng.next_float01() * 6.28318f; // desync the idle wobble
    reg.emplace<Pickup>(e, pk);
    return e;
}

void World::tick(const PlayerCmd& cmd, float dt) {
    copy_prev_transforms(reg);
    if (!player_dead) {
        player_apply_cmd(*this, cmd, dt);
        player_combat(*this, cmd, dt);
        if (!content.enemies.empty()) {
            enemy_ai_think(*this, dt);
        }
        move_and_collide(*this, dt);
        enemy_separation(*this, dt);
        projectiles_update(*this, dt);
        items_update(*this, dt);

        // Standing on the stairs asks the run owner for the next floor.
        const glm::vec2 ppos = reg.get<Transform>(player).pos;
        if (glm::ivec2{static_cast<int>(ppos.x), static_cast<int>(ppos.y)} == dungeon.exit_pos) {
            floor_exit_requested = true;
        }

        if (tick_count % 15 == 0) { // 4 Hz movement sample for the boss brain
            const auto& tr = reg.get<Transform>(player);
            const auto& vel = reg.get<Velocity>(player);
            TelemetryEvent ev;
            ev.tick = static_cast<uint32_t>(tick_count);
            ev.type = EvType::PlayerMoveSample;
            ev.x = tr.pos.x;
            ev.y = tr.pos.y;
            ev.yaw = tr.yaw;
            ev.a = vel.v.x;
            ev.b = vel.v.y;
            telem.record(ev);
        }
    }
    cleanup_dead(*this, dt);
    ++tick_count;
}

void World::apply_content(ContentDB new_content) {
    std::vector<entt::entity> doomed;
    for (auto [e, enemy] : reg.view<Enemy>().each()) {
        const std::string& old_id = content.enemies[enemy.def].id;
        const int idx = new_content.find_enemy(old_id);
        if (idx < 0) {
            doomed.push_back(e);
        } else {
            enemy.def = static_cast<uint16_t>(idx);
        }
    }
    for (auto [e, pickup] : reg.view<Pickup>().each()) {
        const std::string& old_id = content.items[pickup.item].id;
        const int idx = new_content.find_item(old_id);
        if (idx < 0) {
            doomed.push_back(e);
        } else {
            pickup.item = static_cast<uint16_t>(idx);
        }
    }
    for (const entt::entity e : doomed) {
        reg.destroy(e);
    }

    // Held items and feats: remap indices by id and keep StatBlock modifier
    // sources in step; modifiers from removed defs go away. Each modifier is
    // rewritten exactly once from its OLD value in a single pass over one
    // combined remap table, so swapped indices (0<->1) can't cascade.
    {
        constexpr uint16_t kGone = 0xfffe;
        std::vector<std::pair<uint16_t, uint16_t>> remap; // old source -> new (kGone = removed)

        if (auto* inv = reg.try_get<Inventory>(player)) {
            std::vector<uint16_t> kept;
            for (const uint16_t old_idx : inv->items) {
                const int idx = new_content.find_item(content.items[old_idx].id);
                remap.emplace_back(old_idx, idx < 0 ? kGone : static_cast<uint16_t>(idx));
                if (idx >= 0) {
                    kept.push_back(static_cast<uint16_t>(idx));
                }
            }
            inv->items = std::move(kept);
        }
        if (auto* feats = reg.try_get<FeatSet>(player)) {
            std::vector<FeatSet::Entry> kept;
            for (const FeatSet::Entry& entry : feats->entries) {
                const int idx = new_content.find_feat(content.feats[entry.feat].id);
                remap.emplace_back(static_cast<uint16_t>(kFeatSourceBase + entry.feat),
                                   idx < 0 ? kGone
                                           : static_cast<uint16_t>(kFeatSourceBase + idx));
                if (idx >= 0) {
                    kept.push_back({static_cast<uint16_t>(idx), entry.count});
                }
            }
            feats->entries = std::move(kept);
        }

        if (!remap.empty()) {
            auto& stats = reg.get<StatBlock>(player);
            for (Modifier& m : stats.mods) {
                for (const auto& [from, to] : remap) {
                    if (m.source == from) {
                        m.source = to;
                        break;
                    }
                }
            }
            std::erase_if(stats.mods, [](const Modifier& m) { return m.source == kGone; });
            refresh_player_stats();
        }
    }

    content = std::move(new_content);
    log_info("content reloaded: {} enemy defs, {} item defs, {} live entities removed",
             content.enemies.size(), content.items.size(), doomed.size());
}

void World::refresh_player_stats() {
    auto& stats = reg.get<StatBlock>(player);
    stats.recompute();
    auto& hp = reg.get<Health>(player);
    const float gained = stats.cached.max_hp - hp.max_hp;
    hp.max_hp = stats.cached.max_hp;
    hp.hp = std::clamp(hp.hp + std::max(gained, 0.0f), 0.0f, hp.max_hp);
}

void World::on_player_dash(glm::vec2 dir) {
    const auto& tr = reg.get<Transform>(player);
    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(tick_count);
    ev.type = EvType::PlayerDash;
    ev.x = tr.pos.x;
    ev.y = tr.pos.y;
    ev.yaw = tr.yaw;
    ev.a = dir.x;
    ev.b = dir.y;
    telem.record(ev);
}

} // namespace ds
