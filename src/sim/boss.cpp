#include "sim/boss.hpp"

#include "core/log.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/items.hpp"
#include "sim/progression.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

constexpr float kSlamWindup = 0.9f;
constexpr float kChargeWindup = 0.5f;
constexpr float kChargeDuration = 0.8f;
constexpr float kContactCooldown = 1.0f;
constexpr int kMaxAliveAdds = 12;

void record_boss_event(World& world, EvType type, uint16_t def, float a = 0.0f, float b = 0.0f) {
    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = type;
    ev.def = def;
    ev.a = a;
    ev.b = b;
    world.telem.record(ev);
}

// Weighted pattern pick, scaled by the wyrm brain's counter-tag weights.
int pick_pattern(const BossDef& def, const BossTagWeights& tags, Rng& rng) {
    float total = 0.0f;
    for (const BossPatternDef& p : def.patterns) {
        total += p.weight * tags[static_cast<size_t>(boss_pattern_tag(p.pattern))];
    }
    if (total <= 0.0f) {
        return -1;
    }
    float roll = rng.next_float01() * total;
    int last = -1;
    for (size_t i = 0; i < def.patterns.size(); ++i) {
        const BossPatternDef& p = def.patterns[i];
        const float w = p.weight * tags[static_cast<size_t>(boss_pattern_tag(p.pattern))];
        if (w <= 0.0f) {
            continue;
        }
        last = static_cast<int>(i);
        roll -= w;
        if (roll <= 0.0f) {
            return last;
        }
    }
    return last;
}

void start_pattern(World& world, Boss& boss, Transform& tr, const BossDef& def, int index) {
    boss.active_pattern = static_cast<uint8_t>(index);
    boss.pattern_time = 0.0f;
    const BossPatternDef& p = def.patterns[static_cast<size_t>(index)];
    record_boss_event(world, EvType::BossPattern, boss.def, static_cast<float>(p.pattern),
                      static_cast<float>(boss.phase));

    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;
    switch (p.pattern) {
    case BossPattern::GroundSlam:
        boss.slam_pos = player_pos; // marked where you stood — move!
        break;
    case BossPattern::Charge: {
        const glm::vec2 to = player_pos - tr.pos;
        const float len = glm::length(to);
        boss.charge_dir = len > 1e-4f ? to / len : glm::vec2{1.0f, 0.0f};
        break;
    }
    case BossPattern::SummonAdds:
    case BossPattern::ProjectileRing:
        break;
    }
}

// Returns true while the pattern still runs.
bool step_pattern(World& world, entt::entity e, Boss& boss, Transform& tr, Velocity& vel,
                  const BossDef& def, float dt) {
    const BossPatternDef& p = def.patterns[boss.active_pattern];
    boss.pattern_time += dt;
    const float speed_mult = boss.phase >= 2 ? 1.25f : 1.0f;

    switch (p.pattern) {
    case BossPattern::GroundSlam: {
        vel.v = {0.0f, 0.0f}; // rooted while the ground cracks
        if (boss.pattern_time * speed_mult < kSlamWindup) {
            return true;
        }
        const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;
        const glm::vec2 d = player_pos - boss.slam_pos;
        if (glm::dot(d, d) <= p.radius * p.radius) {
            damage_player(world, p.damage, 0xffff, boss.slam_pos);
        }
        return false;
    }
    case BossPattern::Charge: {
        const float t = boss.pattern_time * speed_mult;
        if (t < kChargeWindup) {
            vel.v = {0.0f, 0.0f}; // coiling
            return true;
        }
        if (t >= kChargeWindup + kChargeDuration) {
            vel.v = {0.0f, 0.0f};
            return false;
        }
        vel.v = boss.charge_dir * p.speed;
        const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;
        const float reach = def.radius + world.reg.get<Body>(world.player).radius;
        const glm::vec2 d = player_pos - tr.pos;
        if (glm::dot(d, d) <= reach * reach) {
            damage_player(world, p.damage, 0xffff, tr.pos);
            vel.v = {0.0f, 0.0f};
            return false;
        }
        return true;
    }
    case BossPattern::ProjectileRing: {
        vel.v = {0.0f, 0.0f};
        // One radial volley, then done (the cooldown is the real pacing).
        const int n = std::max(p.count, 1);
        for (int i = 0; i < n; ++i) {
            const float ang = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(n);
            const glm::vec2 dir{std::cos(ang), std::sin(ang)};
            spawn_projectile(world, Team::Enemy, /*weapon=*/0xffff, /*src_def=*/0xffff,
                             tr.pos + dir * (def.radius + 0.2f), dir, p.speed, p.damage, 0.14f,
                             4.0f);
        }
        return false;
    }
    case BossPattern::SummonAdds: {
        vel.v = {0.0f, 0.0f};
        int alive = 0;
        for ([[maybe_unused]] auto _ : world.reg.view<Enemy>()) {
            ++alive;
        }
        const int budget = std::min(p.count, kMaxAliveAdds - alive);
        for (int i = 0; i < budget; ++i) {
            const int def_index = pick_enemy_for_floor(world.content, world.current_floor, world.rng);
            if (def_index < 0) {
                break;
            }
            // Try a few spots around the boss; skip walls.
            for (int attempt = 0; attempt < 6; ++attempt) {
                const float ang = world.rng.next_float01() * glm::two_pi<float>();
                const float dist = 1.5f + world.rng.next_float01() * 1.5f;
                const glm::vec2 pos = tr.pos + glm::vec2{std::cos(ang), std::sin(ang)} * dist;
                if (!world.map().solid(static_cast<int>(pos.x), static_cast<int>(pos.y))) {
                    world.spawn_enemy(def_index, pos);
                    break;
                }
            }
        }
        return false;
    }
    }
    return false;
}

} // namespace

CounterTag boss_pattern_tag(BossPattern pattern) {
    switch (pattern) {
    case BossPattern::GroundSlam: return CounterTag::Melee;    // punishes standing at its feet
    case BossPattern::SummonAdds: return CounterTag::Turtle;   // punishes holding ground
    case BossPattern::Charge: return CounterTag::Kite;         // closes the gap
    case BossPattern::ProjectileRing: return CounterTag::Ranged; // contests mid-range space
    }
    return CounterTag::Melee;
}

void boss_ai_think(World& world, float dt) {
    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;

    for (auto [e, boss, tr, vel, hp] :
         world.reg.view<Boss, Transform, Velocity, Health>().each()) {
        const BossDef& def = world.content.bosses[boss.def];

        if (!boss.engaged) {
            const glm::vec2 d = player_pos - tr.pos;
            if (glm::dot(d, d) <= def.aggro_radius * def.aggro_radius) {
                boss.engaged = true;
                boss.engage_tick = static_cast<uint32_t>(world.tick_count);
                boss.player_hp_at_engage = world.reg.get<Health>(world.player).hp;
                record_boss_event(world, EvType::BossEngaged, boss.def);
            }
            continue;
        }

        if (boss.phase == 1 && hp.max_hp > 0.0f && hp.hp / hp.max_hp <= def.phase2_at) {
            boss.phase = 2; // quicker patterns, shorter rests
        }
        boss.contact_cooldown = std::max(0.0f, boss.contact_cooldown - dt);

        if (boss.active_pattern != 0xff) {
            if (!step_pattern(world, e, boss, tr, vel, def, dt)) {
                const BossPatternDef& p = def.patterns[boss.active_pattern];
                boss.pattern_cooldown = p.cooldown_s * (boss.phase >= 2 ? 0.6f : 1.0f);
                boss.active_pattern = 0xff;
            }
            continue;
        }

        // Between patterns: lumber straight at the player (arenas are open),
        // brush damage on contact, and count down to the next pattern.
        const glm::vec2 to = player_pos - tr.pos;
        const float dist = glm::length(to);
        vel.v = dist > 1e-4f ? to / dist * def.speed : glm::vec2{0.0f};

        const float reach = def.radius + world.reg.get<Body>(world.player).radius + 0.1f;
        if (dist <= reach && boss.contact_cooldown <= 0.0f && !world.player_dead) {
            damage_player(world, def.contact_damage, 0xffff, tr.pos);
            boss.contact_cooldown = kContactCooldown;
        }

        boss.pattern_cooldown -= dt;
        if (boss.pattern_cooldown <= 0.0f && !def.patterns.empty()) {
            const int pick = pick_pattern(def, world.boss_tag_weights, world.rng);
            if (pick >= 0) {
                start_pattern(world, boss, tr, def, pick);
            } else {
                boss.pattern_cooldown = 2.0f; // all weights zeroed: just stalk
            }
        }
    }
}

void damage_boss(World& world, entt::entity boss_e, float amount, int weapon_idx) {
    auto& hp = world.reg.get<Health>(boss_e);
    if (hp.hp <= 0.0f) {
        return;
    }
    hp.hp -= amount;
    world.reg.emplace_or_replace<HurtFlash>(boss_e, 1.0f);
    if (hp.hp > 0.0f) {
        return;
    }
    hp.hp = 0.0f;

    const auto& boss = world.reg.get<Boss>(boss_e);
    const auto& tr = world.reg.get<Transform>(boss_e);
    const BossDef& def = world.content.bosses[boss.def];
    world.score += def.score;
    award_xp(world, static_cast<float>(def.xp));

    const float fight_s =
        static_cast<float>(world.tick_count - boss.engage_tick) / 60.0f;
    const float hp_lost =
        std::max(0.0f, boss.player_hp_at_engage - world.reg.get<Health>(world.player).hp);
    record_boss_event(world, EvType::BossKilled, boss.def, fight_s, hp_lost);
    log_info("boss down: {} in {:.1f}s (weapon {})", def.id, fight_s, weapon_idx);

    world.reg.emplace_or_replace<Doomed>(boss_e);
    world.boss_entity = entt::null; // the seal breaks
    items_dispatch(world, ItemHookDef::Trigger::OnKill, {tr.pos});
}

} // namespace ds
