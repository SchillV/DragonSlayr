#pragma once

#include <cstdint>

namespace ds {

struct World;

// XP needed to go from `level` to `level + 1` (sv.xp_base * level^sv.xp_curve).
float xp_to_next(int level);

// Adds kill XP, resolving any number of level-ups (each grants one skill
// point and records LevelUp telemetry).
void award_xp(World& world, float amount);

enum class NodeState : uint8_t { Locked, Available, Purchased };

// State of a node in the world's active tree (index into that tree's nodes).
NodeState node_state(const World& world, int node_index);

// Spends skill points on an Available node and applies its grant (feat stack
// or kSkillSource attribute modifier). Returns false if locked, owned,
// unaffordable, or there is no active tree.
bool purchase_node(World& world, int node_index);

} // namespace ds
