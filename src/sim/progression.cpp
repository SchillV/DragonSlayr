#include "sim/progression.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "sim/components.hpp"
#include "sim/feats.hpp"
#include "sim/stats.hpp"
#include "sim/telemetry.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

CVar& sv_xp_base = cvar_register("sv.xp_base", 60.0f, "xp for the first level-up");
CVar& sv_xp_curve = cvar_register("sv.xp_curve", 1.4f, "level requirement exponent");

const SkillTreeDef* active_tree(const World& world) {
    if (world.active_tree < 0 ||
        static_cast<size_t>(world.active_tree) >= world.content.skill_trees.size()) {
        return nullptr;
    }
    return &world.content.skill_trees[static_cast<size_t>(world.active_tree)];
}

} // namespace

float xp_to_next(int level) {
    return sv_xp_base.value * std::pow(static_cast<float>(std::max(level, 1)), sv_xp_curve.value);
}

void award_xp(World& world, float amount) {
    if (world.player_dead || amount <= 0.0f) {
        return;
    }
    world.xp += amount;
    while (world.xp >= xp_to_next(world.level)) {
        world.xp -= xp_to_next(world.level);
        ++world.level;
        ++world.skill_points;

        TelemetryEvent ev;
        ev.tick = static_cast<uint32_t>(world.tick_count);
        ev.type = EvType::LevelUp;
        ev.a = static_cast<float>(world.level);
        ev.b = xp_to_next(world.level);
        world.telem.record(ev);
    }
}

NodeState node_state(const World& world, int node_index) {
    const SkillTreeDef* tree = active_tree(world);
    if (!tree || node_index < 0 || static_cast<size_t>(node_index) >= tree->nodes.size()) {
        return NodeState::Locked;
    }
    const auto owned = [&world](int idx) {
        return std::find(world.purchased_nodes.begin(), world.purchased_nodes.end(),
                         static_cast<uint16_t>(idx)) != world.purchased_nodes.end();
    };
    if (owned(node_index)) {
        return NodeState::Purchased;
    }
    for (const int prereq : tree->nodes[static_cast<size_t>(node_index)].prereq_idx) {
        if (!owned(prereq)) {
            return NodeState::Locked;
        }
    }
    return NodeState::Available;
}

bool purchase_node(World& world, int node_index) {
    const SkillTreeDef* tree = active_tree(world);
    if (!tree || node_state(world, node_index) != NodeState::Available) {
        return false;
    }
    const SkillNodeDef& node = tree->nodes[static_cast<size_t>(node_index)];
    if (world.skill_points < node.cost) {
        return false;
    }
    world.skill_points -= node.cost;
    world.purchased_nodes.push_back(static_cast<uint16_t>(node_index));

    if (!node.feat.empty()) {
        const int feat = world.content.find_feat(node.feat);
        if (feat >= 0) {
            grant_feat(world, feat);
        } else {
            log_warn("skill node '{}' references unknown feat '{}'", node.id, node.feat);
        }
    } else {
        auto& stats = world.reg.get<StatBlock>(world.player);
        stats.mods.push_back({node.attr, Modifier::Op::Add,
                              static_cast<float>(node.attr_points), kSkillSource});
        world.refresh_player_stats();
    }

    TelemetryEvent ev;
    ev.tick = static_cast<uint32_t>(world.tick_count);
    ev.type = EvType::SkillPurchase;
    ev.def = static_cast<uint16_t>(node_index);
    ev.flags = static_cast<uint8_t>(world.active_tree);
    ev.a = static_cast<float>(node.cost);
    world.telem.record(ev);
    return true;
}

} // namespace ds
