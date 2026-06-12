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

    // Every spawn point is a walker until spawn tables arrive with floors.
    const int walker = content.find_enemy("walker");
    if (walker >= 0) {
        const EnemyDef& def = content.enemies[static_cast<size_t>(walker)];
        for (const glm::ivec2 sp : dungeon.enemy_spawns) {
            const glm::vec2 pos{static_cast<float>(sp.x) + 0.5f, static_cast<float>(sp.y) + 0.5f};
            const entt::entity e = reg.create();
            reg.emplace<Transform>(e, pos, 0.0f);
            reg.emplace<PrevTransform>(e, pos, 0.0f);
            reg.emplace<Velocity>(e);
            reg.emplace<Body>(e, def.radius);
            reg.emplace<Health>(e, def.hp, def.hp);
            Enemy enemy;
            enemy.def = static_cast<uint16_t>(walker);
            enemy.spawn_tick = 0;
            reg.emplace<Enemy>(e, std::move(enemy));
        }
    } else if (!content.enemies.empty()) {
        log_warn("no 'walker' enemy def; spawning nothing");
    }
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
