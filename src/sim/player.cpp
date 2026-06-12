#include "sim/player.hpp"

#include "core/cvar.hpp"
#include "sim/components.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

// Movement tuning. Legacy game moved ~2.5 tiles/s; these are boomer-shooter
// values on the same dungeon scale, all live-tunable in the console.
CVar& sv_run_speed = cvar_register("sv.run_speed", 8.0f, "base move speed, tiles/s");
CVar& sv_walk_speed = cvar_register("sv.walk_speed", 4.5f, "precision (walk-key) speed, tiles/s");
CVar& sv_accel = cvar_register("sv.accel", 70.0f, "ground acceleration, tiles/s^2");
CVar& sv_friction = cvar_register("sv.friction", 9.0f, "velocity damping, 1/s");
CVar& sv_dash_speed = cvar_register("sv.dash_speed", 16.0f, "dash speed, tiles/s");
CVar& sv_dash_time = cvar_register("sv.dash_time", 0.25f, "dash duration, s");
CVar& sv_dash_cooldown = cvar_register("sv.dash_cooldown", 1.2f, "dash cooldown, s");

} // namespace

void player_apply_cmd(World& world, const PlayerCmd& cmd, float dt) {
    auto& tr = world.reg.get<Transform>(world.player);
    auto& vel = world.reg.get<Velocity>(world.player);
    auto& pl = world.reg.get<Player>(world.player);

    tr.yaw = cmd.yaw;
    world.player_pitch = cmd.pitch;

    // Friction first (Quake-style), so taps decay and direction changes bite.
    const float speed = glm::length(vel.v);
    if (speed > 0.0f) {
        const float drop = speed * sv_friction.value * dt;
        vel.v *= std::max(speed - drop, 0.0f) / speed;
    }

    const float max_speed = cmd.run ? sv_run_speed.value : sv_walk_speed.value;
    const bool has_wish = glm::dot(cmd.wish_dir, cmd.wish_dir) > 0.0f;
    if (has_wish) {
        vel.v += cmd.wish_dir * (sv_accel.value * dt);
        const float new_speed = glm::length(vel.v);
        if (new_speed > max_speed && pl.dash_time_left <= 0.0f) {
            vel.v *= max_speed / new_speed;
        }
    }

    pl.dash_cooldown = std::max(0.0f, pl.dash_cooldown - dt);
    if (cmd.dash && pl.dash_cooldown <= 0.0f) {
        pl.dash_time_left = sv_dash_time.value;
        pl.dash_cooldown = sv_dash_cooldown.value;
        pl.dash_dir = has_wish ? cmd.wish_dir : glm::vec2{std::cos(cmd.yaw), std::sin(cmd.yaw)};
        world.on_player_dash(pl.dash_dir); // telemetry hook (recorded from M5)
    }
    if (pl.dash_time_left > 0.0f) {
        pl.dash_time_left -= dt;
        vel.v = pl.dash_dir * sv_dash_speed.value;
    }
}

} // namespace ds
