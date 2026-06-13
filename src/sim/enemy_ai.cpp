#include "sim/enemy_ai.hpp"

#include "core/cvar.hpp"
#include "sim/collision.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/pathfinding.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

CVar& ai_repath_interval = cvar_register("ai.repath_interval", 0.4f, "seconds between A* repaths");

glm::ivec2 tile_of(glm::vec2 p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
}

void face(Transform& tr, glm::vec2 dir) {
    if (glm::dot(dir, dir) > 1e-8f) {
        tr.yaw = std::atan2(dir.y, dir.x);
    }
}

// A*-follow toward the player, setting velocity and facing. Shared by the
// chaser and charger, and by the ranged kiter when it needs to close in.
void chase_path(World& world, Enemy& enemy, const EnemyDef& def, Transform& tr, Velocity& vel,
                const Body& body, glm::vec2 player_pos, float dt) {
    enemy.repath_timer -= dt;
    if (enemy.repath_timer <= 0.0f) {
        enemy.repath_timer = ai_repath_interval.value;
        const auto tiles = astar(world.map(), tile_of(tr.pos), tile_of(player_pos));
        enemy.path = smooth_path(world.map(), tiles, body.radius);
        enemy.path_index = 0;
    }
    glm::vec2 target = player_pos;
    while (enemy.path_index < enemy.path.size() &&
           glm::length(enemy.path[enemy.path_index] - tr.pos) < 0.2f) {
        ++enemy.path_index;
    }
    if (enemy.path_index < enemy.path.size()) {
        target = enemy.path[enemy.path_index];
    }
    const glm::vec2 to_target = target - tr.pos;
    const float tl = glm::length(to_target);
    vel.v = tl > 1e-4f ? to_target / tl * def.speed : glm::vec2{0.0f};
    face(tr, to_target);
}

// Behavior-specific movement while engaged (state == Chase).
void move_engaged(World& world, Enemy& enemy, const EnemyDef& def, Transform& tr, Velocity& vel,
                  const Body& body, glm::vec2 player_pos, float dist, float dt) {
    switch (def.behavior) {
    case EnemyBehavior::Stationary:
        vel.v = {0.0f, 0.0f};
        face(tr, player_pos - tr.pos);
        break;
    case EnemyBehavior::Ranged: {
        const glm::vec2 to = player_pos - tr.pos;
        face(tr, to);
        if (dist < def.keep_distance * 0.85f) {
            vel.v = dist > 1e-4f ? -to / dist * def.speed : glm::vec2{0.0f}; // back off
        } else if (dist > def.keep_distance * 1.2f) {
            chase_path(world, enemy, def, tr, vel, body, player_pos, dt); // close in
        } else {
            vel.v = {0.0f, 0.0f}; // hold the line and shoot
        }
        break;
    }
    case EnemyBehavior::Chaser:
    case EnemyBehavior::Charger:
        chase_path(world, enemy, def, tr, vel, body, player_pos, dt);
        break;
    }
}

// On windup completion: melee (chaser), fire a projectile (ranged/stationary),
// or commit a lunge (charger). Returns true if the enemy should enter Lunge.
bool resolve_attack(World& world, Enemy& enemy, const EnemyDef& def, Transform& tr,
                    glm::vec2 player_pos, float dist, bool los) {
    switch (def.behavior) {
    case EnemyBehavior::Chaser:
        if (dist <= def.attack.range * def.attack.dodge_window_mult && los) {
            damage_player(world, def.attack.damage, enemy.def);
        }
        return false;
    case EnemyBehavior::Ranged:
    case EnemyBehavior::Stationary: {
        if (!los) {
            return false; // lost line of sight during windup → the shot fizzles
        }
        const glm::vec2 to = player_pos - tr.pos;
        const float l = glm::length(to);
        const glm::vec2 dir = l > 1e-4f ? to / l : glm::vec2{std::cos(tr.yaw), std::sin(tr.yaw)};
        spawn_projectile(world, Team::Enemy, /*weapon=*/0xffff, enemy.def, tr.pos + dir * 0.4f, dir,
                         def.attack.projectile_speed, def.attack.damage,
                         def.attack.projectile_radius, /*ttl=*/4.0f);
        return false;
    }
    case EnemyBehavior::Charger:
        enemy.lunge_dir = dist > 1e-4f ? (player_pos - tr.pos) / dist
                                       : glm::vec2{std::cos(tr.yaw), std::sin(tr.yaw)};
        return true;
    }
    return false;
}

} // namespace

void enemy_ai_think(World& world, float dt) {
    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;

    for (auto [e, enemy, tr, vel, body] :
         world.reg.view<Enemy, Transform, Velocity, Body>().each()) {
        const EnemyDef& def = world.content.enemies[enemy.def];
        const glm::vec2 to_player = player_pos - tr.pos;
        const float dist = glm::length(to_player);
        const bool los = !grid_raycast(world.map(), tr.pos, player_pos);
        enemy.attack_cooldown = std::max(0.0f, enemy.attack_cooldown - dt);

        switch (enemy.state) {
        case AiState::Idle:
            vel.v = {0.0f, 0.0f};
            if (dist < def.aggro_radius && los) {
                enemy.state = AiState::Chase;
                enemy.repath_timer = world.rng.next_float01() * ai_repath_interval.value; // stagger
            }
            break;

        case AiState::Chase:
            move_engaged(world, enemy, def, tr, vel, body, player_pos, dist, dt);
            // attack.range is melee reach (chaser), charge-trigger distance
            // (charger), or max firing distance (ranged/stationary).
            if (dist <= def.attack.range && enemy.attack_cooldown <= 0.0f && los) {
                enemy.state = AiState::Windup;
                enemy.state_time = def.attack.windup_s;
                enemy.attack_cooldown = def.attack.cooldown_s;
                vel.v = {0.0f, 0.0f};
            } else if (dist > def.aggro_radius * 1.75f) {
                enemy.state = AiState::Idle;
                enemy.path.clear();
            }
            break;

        case AiState::Windup:
            vel.v = {0.0f, 0.0f};
            face(tr, to_player);
            enemy.state_time -= dt;
            if (enemy.state_time <= 0.0f) {
                const bool lunge = resolve_attack(world, enemy, def, tr, player_pos, dist, los);
                enemy.state = lunge ? AiState::Lunge : AiState::Recover;
                enemy.state_time = lunge ? 0.4f : def.attack.recovery_s;
            }
            break;

        case AiState::Lunge: { // charger only: a committed, dodgeable dash
            enemy.state_time -= dt;
            vel.v = enemy.lunge_dir * def.lunge_speed;
            const float contact = def.radius + 0.6f; // physical reach, not firing range
            if (dist <= contact && los) {
                damage_player(world, def.attack.damage, enemy.def);
                enemy.state = AiState::Recover;
                enemy.state_time = def.attack.recovery_s;
            } else if (enemy.state_time <= 0.0f) {
                enemy.state = AiState::Recover;
                enemy.state_time = def.attack.recovery_s;
            }
            break;
        }

        case AiState::Recover:
            vel.v = {0.0f, 0.0f};
            enemy.state_time -= dt;
            if (enemy.state_time <= 0.0f) {
                enemy.state = AiState::Chase;
            }
            break;
        }
    }
}

void enemy_separation(World& world, float) {
    auto view = world.reg.view<Enemy, Transform, Body>();
    for (auto a = view.begin(); a != view.end(); ++a) {
        auto b = a;
        for (++b; b != view.end(); ++b) {
            auto& tra = view.get<Transform>(*a);
            auto& trb = view.get<Transform>(*b);
            const float ra = view.get<Body>(*a).radius;
            const float rb = view.get<Body>(*b).radius;
            const float min_dist = ra + rb;
            const glm::vec2 d = trb.pos - tra.pos;
            const float dist2 = glm::dot(d, d);
            if (dist2 >= min_dist * min_dist || dist2 < 1e-8f) {
                continue;
            }
            const float dist = std::sqrt(dist2);
            const glm::vec2 push = d / dist * (min_dist - dist) * 0.5f;
            // Resolve the push through collision so nobody gets shoved into a wall.
            tra.pos = slide_move(world.map(), tra.pos, ra, -push).pos;
            trb.pos = slide_move(world.map(), trb.pos, rb, push).pos;
        }
    }
}

} // namespace ds
