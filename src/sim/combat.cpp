#include "sim/combat.hpp"

#include "core/cvar.hpp"
#include "sim/collision.hpp"
#include "sim/components.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ds {

namespace {

CVar& sv_god = cvar_register("sv.god", 0.0f, "player takes no damage", CVAR_CHEAT);

// Tag marking entities for destruction at the end of the tick.
struct Doomed {};

float wrap_angle(float a) {
    const float two_pi = glm::two_pi<float>();
    a = std::fmod(a + glm::pi<float>(), two_pi);
    if (a < 0.0f) {
        a += two_pi;
    }
    return a - glm::pi<float>();
}

float seg_point_dist2(glm::vec2 a, glm::vec2 b, glm::vec2 p) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 < 1e-12f) {
        const glm::vec2 d = p - a;
        return glm::dot(d, d);
    }
    const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
    const glm::vec2 d = p - (a + ab * t);
    return glm::dot(d, d);
}

} // namespace

bool in_melee_arc(glm::vec2 origin, float yaw, float range, float arc_deg, glm::vec2 target) {
    const glm::vec2 to = target - origin;
    const float dist = glm::length(to);
    if (dist > range) {
        return false;
    }
    if (dist < 1e-4f) {
        return true;
    }
    const float ang = std::atan2(to.y, to.x);
    return std::abs(wrap_angle(ang - yaw)) <= glm::radians(arc_deg) * 0.5f;
}

entt::entity spawn_projectile(World& world, Team team, uint16_t weapon, uint16_t src_def,
                              glm::vec2 pos, glm::vec2 dir, float speed, float damage, float radius,
                              float ttl) {
    const float yaw = std::atan2(dir.y, dir.x);
    const entt::entity p = world.reg.create();
    world.reg.emplace<Transform>(p, pos, yaw);
    world.reg.emplace<PrevTransform>(p, pos, yaw);
    world.reg.emplace<Velocity>(p, dir * speed);
    Projectile proj;
    proj.team = team;
    proj.weapon = weapon;
    proj.src_def = src_def;
    proj.damage = damage;
    proj.radius = radius;
    proj.ttl = ttl;
    world.reg.emplace<Projectile>(p, proj);
    return p;
}

void player_combat(World& world, const PlayerCmd& cmd, float dt) {
    auto& pl = world.reg.get<Player>(world.player);
    const auto& tr = world.reg.get<Transform>(world.player);

    pl.primary_cooldown = std::max(0.0f, pl.primary_cooldown - dt);
    pl.secondary_cooldown = std::max(0.0f, pl.secondary_cooldown - dt);
    pl.swing_anim = std::max(0.0f, pl.swing_anim - dt / 0.28f);
    pl.cast_anim = std::max(0.0f, pl.cast_anim - dt / 0.25f);
    pl.hurt_flash = std::max(0.0f, pl.hurt_flash - dt * 2.5f);

    // Sword.
    if (cmd.attack_primary && pl.primary_cooldown <= 0.0f && world.primary_weapon >= 0) {
        const WeaponDef& w = world.content.weapons[static_cast<size_t>(world.primary_weapon)];
        pl.primary_cooldown = w.cooldown_s;
        pl.swing_anim = 1.0f;

        bool any_hit = false;
        uint16_t hit_def = 0xffff;
        for (auto [e, enemy, etr] : world.reg.view<Enemy, Transform>().each()) {
            if (!in_melee_arc(tr.pos, cmd.yaw, w.range, w.arc_deg, etr.pos)) {
                continue;
            }
            if (grid_raycast(world.map(), tr.pos, etr.pos)) {
                continue; // wall between us
            }
            damage_enemy(world, e, w.damage, world.primary_weapon);
            any_hit = true;
            hit_def = enemy.def;
        }

        TelemetryEvent ev;
        ev.tick = static_cast<uint32_t>(world.tick_count);
        ev.type = EvType::PlayerAttack;
        ev.flags = static_cast<uint8_t>((any_hit ? 1 : 0) |
                                        (static_cast<unsigned>(world.primary_weapon) << 1));
        ev.def = hit_def;
        ev.a = w.damage;
        ev.x = tr.pos.x;
        ev.y = tr.pos.y;
        ev.yaw = cmd.yaw;
        world.telem.record(ev);
    }

    // Arcane bolt.
    if (cmd.attack_secondary && pl.secondary_cooldown <= 0.0f && world.secondary_weapon >= 0) {
        const WeaponDef& w = world.content.weapons[static_cast<size_t>(world.secondary_weapon)];
        pl.secondary_cooldown = w.cooldown_s;
        pl.cast_anim = 1.0f;

        const glm::vec2 dir{std::cos(cmd.yaw), std::sin(cmd.yaw)};
        spawn_projectile(world, Team::Player, static_cast<uint16_t>(world.secondary_weapon),
                         /*src_def=*/0xffff, tr.pos + dir * 0.4f, dir, w.speed, w.damage, w.radius,
                         w.ttl_s);

        TelemetryEvent ev;
        ev.tick = static_cast<uint32_t>(world.tick_count);
        ev.type = EvType::ProjectileFired;
        ev.def = static_cast<uint16_t>(world.secondary_weapon);
        ev.x = tr.pos.x;
        ev.y = tr.pos.y;
        ev.yaw = cmd.yaw;
        world.telem.record(ev);
    }
}

void projectiles_update(World& world, float dt) {
    // Projectiles have no Body on purpose: move_and_collide skips them and
    // they sweep the grid themselves (a moving point can't tunnel a raycast).
    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;
    const float player_r = world.reg.get<Body>(world.player).radius;

    for (auto [e, proj, tr, vel] : world.reg.view<Projectile, Transform, Velocity>().each()) {
        proj.ttl -= dt;
        if (proj.ttl <= 0.0f) {
            world.reg.emplace_or_replace<Doomed>(e);
            continue;
        }
        const glm::vec2 next = tr.pos + vel.v * dt;
        bool consumed = false;

        if (proj.team == Team::Player) {
            for (auto [en, enemy, etr, ebody] : world.reg.view<Enemy, Transform, Body>().each()) {
                const float reach = proj.radius + ebody.radius;
                if (seg_point_dist2(tr.pos, next, etr.pos) <= reach * reach) {
                    damage_enemy(world, en, proj.damage,
                                 proj.weapon == 0xffff ? -1 : static_cast<int>(proj.weapon));

                    TelemetryEvent ev;
                    ev.tick = static_cast<uint32_t>(world.tick_count);
                    ev.type = EvType::ProjectileHit;
                    ev.def = enemy.def;
                    ev.a = proj.damage;
                    ev.x = etr.pos.x;
                    ev.y = etr.pos.y;
                    world.telem.record(ev);

                    world.reg.emplace_or_replace<Doomed>(e);
                    consumed = true;
                    break;
                }
            }
        } else if (!world.player_dead) {
            const float reach = proj.radius + player_r;
            if (seg_point_dist2(tr.pos, next, player_pos) <= reach * reach) {
                damage_player(world, proj.damage, proj.src_def); // records PlayerDamaged
                world.reg.emplace_or_replace<Doomed>(e);
                consumed = true;
            }
        }
        if (consumed) {
            continue;
        }

        glm::ivec2 hit_tile;
        if (grid_raycast(world.map(), tr.pos, next, &hit_tile)) {
            if (proj.team == Team::Player) {
                TelemetryEvent ev;
                ev.tick = static_cast<uint32_t>(world.tick_count);
                ev.type = EvType::ProjectileHit;
                ev.def = 0xffff; // wall
                ev.x = static_cast<float>(hit_tile.x) + 0.5f;
                ev.y = static_cast<float>(hit_tile.y) + 0.5f;
                world.telem.record(ev);
            }
            world.reg.emplace_or_replace<Doomed>(e);
            continue;
        }
        tr.pos = next;
    }
}

void damage_enemy(World& world, entt::entity enemy_e, float amount, int weapon_idx) {
    auto& hp = world.reg.get<Health>(enemy_e);
    if (hp.hp <= 0.0f) {
        return; // already dying this tick
    }
    hp.hp -= amount;
    world.reg.emplace_or_replace<HurtFlash>(enemy_e, 1.0f);
    if (hp.hp > 0.0f) {
        return;
    }
    hp.hp = 0.0f;
    const auto& enemy = world.reg.get<Enemy>(enemy_e);
    const auto& etr = world.reg.get<Transform>(enemy_e);
    const EnemyDef& def = world.content.enemies[enemy.def];
    world.score += def.score;

    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = EvType::EnemyKilled;
    ev.def = enemy.def;
    ev.a = static_cast<float>(weapon_idx);
    ev.b = static_cast<float>(world.tick_count - enemy.spawn_tick) / 60.0f;
    ev.x = etr.pos.x;
    ev.y = etr.pos.y;
    world.telem.record(ev);

    world.reg.emplace_or_replace<Doomed>(enemy_e);
}

void damage_player(World& world, float amount, uint16_t src_def) {
    if (sv_god.as_bool() || world.player_dead) {
        return;
    }
    auto& hp = world.reg.get<Health>(world.player);
    auto& pl = world.reg.get<Player>(world.player);
    const auto& tr = world.reg.get<Transform>(world.player);

    hp.hp -= amount;
    pl.hurt_flash = 1.0f;

    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = EvType::PlayerDamaged;
    ev.def = src_def;
    ev.a = amount;
    ev.b = std::max(hp.hp, 0.0f);
    ev.x = tr.pos.x;
    ev.y = tr.pos.y;
    world.telem.record(ev);

    if (hp.hp <= 0.0f) {
        hp.hp = 0.0f;
        world.player_dead = true;
        TelemetryEvent end;
        end.tick = static_cast<uint32_t>(world.tick_count);
        end.type = EvType::RunEnd;
        world.telem.record(end);
    }
}

void cleanup_dead(World& world, float dt) {
    // Decay hurt flashes; drop the component once invisible.
    std::vector<entt::entity> expired;
    for (auto [e, flash] : world.reg.view<HurtFlash>().each()) {
        flash.t -= dt * 5.0f;
        if (flash.t <= 0.0f) {
            expired.push_back(e);
        }
    }
    for (const entt::entity e : expired) {
        world.reg.remove<HurtFlash>(e);
    }

    const auto doomed = world.reg.view<Doomed>();
    world.reg.destroy(doomed.begin(), doomed.end());
}

} // namespace ds
