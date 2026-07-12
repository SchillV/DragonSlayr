#pragma once

#include "sim/content.hpp"

#include <array>
#include <entt/entt.hpp>

namespace ds {

struct World;

// What player style a pattern punishes — the wyrm brain's vocabulary. The
// mapping is fixed C++ knowledge (boss_pattern_tag); the brain only tunes
// per-tag weights.
enum class CounterTag : uint8_t { Melee, Ranged, Kite, Turtle, Count };
constexpr size_t kCounterTagCount = static_cast<size_t>(CounterTag::Count);

CounterTag boss_pattern_tag(BossPattern pattern);

// Per-tag pattern-weight multipliers, injected by the run owner (the wyrm
// brain in H5); all 1.0 = data weights only.
using BossTagWeights = std::array<float, kCounterTagCount>;

// The boss system: engage/chase, contact damage, pattern selection and
// execution, phase two. Runs after enemy_ai in the fixed tick order.
void boss_ai_think(World& world, float dt);

// Player weapons route boss hits here (bosses are not Enemy entities).
// Handles kill: score/xp, hooks, telemetry, unsealing the stairs.
void damage_boss(World& world, entt::entity boss_e, float amount, int weapon_idx);

} // namespace ds
