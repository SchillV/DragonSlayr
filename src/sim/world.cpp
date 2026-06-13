#include "sim/world.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "sim/collision.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/enemy_ai.hpp"

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
    reg.clear();
    dungeon = std::move(d);
    seed = s;
    rng = Rng(s ^ 0x9e3779b97f4a7c15ULL); // distinct stream from generation
    tick_count = 0;
    player_dead = false;
    score = 0;
    cvar_reset_cheat_touched();

    primary_weapon = content.find_weapon("sword");
    secondary_weapon = content.find_weapon("bolt");

    telem.begin_run(s);
    TelemetryEvent start;
    start.type = EvType::RunStart;
    telem.record(start);

    player = reg.create();
    const glm::vec2 spawn{static_cast<float>(dungeon.player_spawn.x) + 0.5f,
                          static_cast<float>(dungeon.player_spawn.y) + 0.5f};
    reg.emplace<Transform>(player, spawn, 0.0f);
    reg.emplace<PrevTransform>(player, spawn, 0.0f);
    reg.emplace<Velocity>(player);
    reg.emplace<Body>(player, 0.3f);
    reg.emplace<Health>(player, 100.0f, 100.0f);
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

int pick_enemy_for_floor(const ContentDB& content, int floor, Rng& rng) {
    auto eligible = [&](const EnemyDef& d) { return d.min_floor <= floor && d.spawn_weight > 0.0f; };
    float total = 0.0f;
    for (const EnemyDef& d : content.enemies) {
        if (eligible(d)) {
            total += d.spawn_weight;
        }
    }
    if (total <= 0.0f) {
        return -1;
    }
    float roll = rng.next_float01() * total;
    int last = -1;
    for (size_t i = 0; i < content.enemies.size(); ++i) {
        if (!eligible(content.enemies[i])) {
            continue;
        }
        last = static_cast<int>(i);
        roll -= content.enemies[i].spawn_weight;
        if (roll <= 0.0f) {
            return static_cast<int>(i);
        }
    }
    return last; // float rounding fallthrough → last eligible def
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
    for (const entt::entity e : doomed) {
        reg.destroy(e);
    }
    content = std::move(new_content);
    log_info("content reloaded: {} enemy defs, {} live enemies removed", content.enemies.size(),
             doomed.size());
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
