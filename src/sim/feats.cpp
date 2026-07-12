#include "sim/feats.hpp"

#include "sim/components.hpp"
#include "sim/stats.hpp"
#include "sim/telemetry.hpp"
#include "sim/world.hpp"

#include <algorithm>

namespace ds {

namespace {

FeatSet::Entry* find_entry(FeatSet& set, uint16_t feat) {
    for (FeatSet::Entry& e : set.entries) {
        if (e.feat == feat) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

int grant_feat(World& world, int feat_index) {
    if (feat_index < 0 || static_cast<size_t>(feat_index) >= world.content.feats.size()) {
        return 0;
    }
    const FeatDef& def = world.content.feats[static_cast<size_t>(feat_index)];
    auto& set = world.reg.get_or_emplace<FeatSet>(world.player);

    FeatSet::Entry* entry = find_entry(set, static_cast<uint16_t>(feat_index));
    if (!entry) {
        set.entries.push_back({static_cast<uint16_t>(feat_index), 0});
        entry = &set.entries.back();
    }
    if (def.max_stacks > 0 && entry->count >= def.max_stacks) {
        return 0; // maxed out
    }
    ++entry->count;

    auto& stats = world.reg.get<StatBlock>(world.player);
    const auto source = static_cast<uint16_t>(kFeatSourceBase + feat_index);
    for (const ItemModifierDef& m : def.modifiers) {
        stats.mods.push_back({m.stat, m.op, m.value, source});
    }
    world.refresh_player_stats();

    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = EvType::FeatGained;
    ev.def = static_cast<uint16_t>(feat_index);
    ev.a = static_cast<float>(entry->count);
    world.telem.record(ev);
    return entry->count;
}

int feat_stacks(const World& world, int feat_index) {
    const auto* set = world.reg.try_get<FeatSet>(world.player);
    if (!set) {
        return 0;
    }
    for (const FeatSet::Entry& e : set->entries) {
        if (e.feat == feat_index) {
            return e.count;
        }
    }
    return 0;
}

void remove_feat(World& world, int feat_index) {
    auto* set = world.reg.try_get<FeatSet>(world.player);
    if (!set) {
        return;
    }
    std::erase_if(set->entries,
                  [feat_index](const FeatSet::Entry& e) { return e.feat == feat_index; });
    auto& stats = world.reg.get<StatBlock>(world.player);
    stats.remove_source(static_cast<uint16_t>(kFeatSourceBase + feat_index));
    world.refresh_player_stats();
}

} // namespace ds
