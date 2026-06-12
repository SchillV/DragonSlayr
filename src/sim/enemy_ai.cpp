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

} // namespace

void enemy_ai_think(World& world, float dt) {
    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;

    for (auto [e, enemy, tr, vel, body] :
         world.reg.view<Enemy, Transform, Velocity, Body>().each()) {
        const EnemyDef& def = world.content.enemies[enemy.def];
        const glm::vec2 to_player = player_pos - tr.pos;
        const float dist = glm::length(to_player);
        enemy.attack_cooldown = std::max(0.0f, enemy.attack_cooldown - dt);

        switch (enemy.state) {
        case AiState::Idle:
            vel.v = {0.0f, 0.0f};
            if (dist < def.aggro_radius && !grid_raycast(world.map(), tr.pos, player_pos)) {
                enemy.state = AiState::Chase;
                enemy.repath_timer = world.rng.next_float01() * ai_repath_interval.value; // stagger
            }
            break;

        case AiState::Chase: {
            enemy.repath_timer -= dt;
            if (enemy.repath_timer <= 0.0f) {
                enemy.repath_timer = ai_repath_interval.value;
                const auto tiles = astar(world.map(), tile_of(tr.pos), tile_of(player_pos));
                enemy.path = smooth_path(world.map(), tiles, body.radius);
                enemy.path_index = 0;
            }

            // Advance past waypoints we've reached; head for the next one.
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
            if (tl > 1e-4f) {
                tr.yaw = std::atan2(to_target.y, to_target.x);
            }

            // Telegraphed attack: stop, wind up, then resolve.
            if (dist <= def.attack.range && enemy.attack_cooldown <= 0.0f &&
                !grid_raycast(world.map(), tr.pos, player_pos)) {
                enemy.state = AiState::Windup;
                enemy.state_time = def.attack.windup_s;
                enemy.attack_cooldown = def.attack.cooldown_s;
                vel.v = {0.0f, 0.0f};
            } else if (dist > def.aggro_radius * 1.75f) {
                enemy.state = AiState::Idle;
                enemy.path.clear();
            }
            break;
        }

        case AiState::Windup:
            vel.v = {0.0f, 0.0f};
            enemy.state_time -= dt;
            if (enemy.state_time <= 0.0f) {
                // The hit lands only if the player is still in reach — windups
                // are dodgeable, that's the point.
                if (dist <= def.attack.range * 1.3f &&
                    !grid_raycast(world.map(), tr.pos, player_pos)) {
                    damage_player(world, def.attack.damage, enemy.def);
                }
                enemy.state = AiState::Recover;
                enemy.state_time = 0.25f;
            }
            break;

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
