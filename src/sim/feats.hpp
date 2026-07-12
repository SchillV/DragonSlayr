#pragma once

namespace ds {

struct World;

// Grants one stack of a feat: records it in the player's FeatSet, applies the
// def's modifiers (source = kFeatSourceBase + index, once per stack), fires
// nothing (feats have no on-grant hook; use on_pickup semantics via items if
// needed). Returns the new stack count, or 0 if already at max_stacks.
int grant_feat(World& world, int feat_index);

// Current stack count (0 = not held).
int feat_stacks(const World& world, int feat_index);

// Removes ALL stacks of a feat and its modifiers (hot reload / respec).
void remove_feat(World& world, int feat_index);

} // namespace ds
