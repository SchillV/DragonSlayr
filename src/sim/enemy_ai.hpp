#pragma once

namespace ds {

struct World;

// Chaser brain: idle until the player is close and visible, then A*-chase
// with staggered repaths. Attack states arrive with the combat system (M5).
void enemy_ai_think(World& world, float dt);

// Cheap pairwise push-apart so chasers don't stack inside each other.
void enemy_separation(World& world, float dt);

} // namespace ds
