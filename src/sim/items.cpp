#include "sim/items.hpp"

#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <vector>

namespace ds {

namespace {

// Temp-modifier source tokens live far above any plausible item index so the
// two id spaces (and hot-reload remapping, which only touches item indices)
// can never collide.
constexpr uint16_t kTempSourceBase = 0xf000;
constexpr uint16_t kTempSourceSpan = 0x0e00; // wraps before 0xfffe/0xffff sentinels

constexpr float kPickupRadius = 0.65f; // walk-over distance, tiles

void apply_hook(World& world, const ItemHookDef& hook, const HookCtx& ctx) {
    switch (hook.effect) {
    case ItemHookDef::Effect::Heal: {
        auto& hp = world.reg.get<Health>(world.player);
        hp.hp = std::min(hp.hp + hook.amount, hp.max_hp);
        break;
    }
    case ItemHookDef::Effect::TempStat: {
        auto& temp = world.reg.get_or_emplace<TempMods>(world.player);
        const uint16_t token =
            static_cast<uint16_t>(kTempSourceBase + (temp.next_token++ % kTempSourceSpan));
        auto& stats = world.reg.get<StatBlock>(world.player);
        stats.remove_source(token); // paranoia if the token space wrapped
        stats.add({hook.stat, hook.op, hook.value, token});
        temp.entries.push_back({token, hook.duration_s});
        world.refresh_player_stats();
        break;
    }
    case ItemHookDef::Effect::AoeDamage: {
        // Snapshot first: damage_enemy mutates components mid-iteration.
        std::vector<entt::entity> victims;
        for (auto [e, enemy, tr] : world.reg.view<Enemy, Transform>().each()) {
            const glm::vec2 d = tr.pos - ctx.pos;
            if (glm::dot(d, d) <= hook.radius * hook.radius) {
                victims.push_back(e);
            }
        }
        for (const entt::entity e : victims) {
            damage_enemy(world, e, hook.amount, /*weapon_idx=*/-1);
        }
        break;
    }
    }
}

} // namespace

void items_dispatch(World& world, ItemHookDef::Trigger trigger, const HookCtx& ctx) {
    // An on_kill AoE can kill more enemies, which dispatches on_kill again.
    // One level of chaining is a synergy; unbounded chaining is a hang.
    if (world.hook_depth >= 2 || world.player_dead) {
        return;
    }
    const auto* inv = world.reg.try_get<Inventory>(world.player);
    if (!inv) {
        return;
    }
    ++world.hook_depth;
    for (const uint16_t item : inv->items) {
        for (const ItemHookDef& hook : world.content.items[item].hooks) {
            if (hook.on == trigger) {
                apply_hook(world, hook, ctx);
            }
        }
    }
    --world.hook_depth;
}

void grant_item(World& world, int item_index) {
    const ItemDef& def = world.content.items[static_cast<size_t>(item_index)];

    auto& inv = world.reg.get_or_emplace<Inventory>(world.player);
    inv.items.push_back(static_cast<uint16_t>(item_index));

    auto& stats = world.reg.get<StatBlock>(world.player);
    for (const ItemModifierDef& m : def.modifiers) {
        stats.add({m.stat, m.op, m.value, static_cast<uint16_t>(item_index)});
    }
    world.refresh_player_stats();

    const auto& tr = world.reg.get<Transform>(world.player);
    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = EvType::ItemPickup;
    ev.def = static_cast<uint16_t>(item_index);
    ev.x = tr.pos.x;
    ev.y = tr.pos.y;
    world.telem.record(ev);

    items_dispatch(world, ItemHookDef::Trigger::OnPickup, {tr.pos});
}

void items_update(World& world, float dt) {
    // Walk-over pickups. Collect first: grant_item mutates player components
    // and could in principle spawn things.
    const glm::vec2 player_pos = world.reg.get<Transform>(world.player).pos;
    std::vector<entt::entity> grabbed;
    for (auto [e, pickup, tr] : world.reg.view<Pickup, Transform>().each()) {
        const glm::vec2 d = tr.pos - player_pos;
        if (glm::dot(d, d) <= kPickupRadius * kPickupRadius) {
            grabbed.push_back(e);
        }
    }
    for (const entt::entity e : grabbed) {
        grant_item(world, world.reg.get<Pickup>(e).item);
        world.reg.destroy(e);
    }

    // Temp-modifier expiry.
    if (auto* temp = world.reg.try_get<TempMods>(world.player)) {
        bool expired_any = false;
        auto& stats = world.reg.get<StatBlock>(world.player);
        for (auto& entry : temp->entries) {
            entry.ttl -= dt;
            if (entry.ttl <= 0.0f) {
                stats.remove_source(entry.source);
                expired_any = true;
            }
        }
        if (expired_any) {
            std::erase_if(temp->entries,
                          [](const TempMods::Entry& e) { return e.ttl <= 0.0f; });
            world.refresh_player_stats();
        }
    }
}

} // namespace ds
