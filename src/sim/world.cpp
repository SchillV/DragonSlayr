#include "sim/world.hpp"

#include "core/cvar.hpp"
#include "sim/collision.hpp"
#include "sim/components.hpp"

#include <utility>

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
    cvar_reset_cheat_touched();

    player = reg.create();
    const glm::vec2 spawn{static_cast<float>(dungeon.player_spawn.x) + 0.5f,
                          static_cast<float>(dungeon.player_spawn.y) + 0.5f};
    reg.emplace<Transform>(player, spawn, 0.0f);
    reg.emplace<PrevTransform>(player, spawn, 0.0f);
    reg.emplace<Velocity>(player);
    reg.emplace<Body>(player, 0.3f);
    reg.emplace<Health>(player, 100.0f, 100.0f);
    reg.emplace<Player>(player);
}

void World::tick(const PlayerCmd& cmd, float dt) {
    copy_prev_transforms(reg);
    player_apply_cmd(*this, cmd, dt);
    move_and_collide(*this, dt);
    ++tick_count;
}

void World::on_player_dash(glm::vec2) {
    // Telemetry recorder attaches here in M5.
}

} // namespace ds
