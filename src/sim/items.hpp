#pragma once

#include "sim/content.hpp"

#include <glm/glm.hpp>

namespace ds {

struct World;

// Context handed to item hooks: where the triggering event happened (the
// struck enemy, the killing blow, the player when damaged).
struct HookCtx {
    glm::vec2 pos{0.0f};
};

// Pickup overlap (grants touched items) + temp-modifier expiry. Runs in the
// fixed tick order after projectiles.
void items_update(World& world, float dt);

// Adds the item to the player's inventory, applies its passive modifiers
// (source = item index), fires its on_pickup hooks, records telemetry.
void grant_item(World& world, int item_index);

// Fires every matching hook on every held item. Re-entrancy is depth-capped
// so an on_kill AoE that kills more enemies can't cascade forever.
void items_dispatch(World& world, ItemHookDef::Trigger trigger, const HookCtx& ctx);

} // namespace ds
