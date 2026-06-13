#pragma once

#include "sim/player.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace ds {

struct World;
enum class Team : uint8_t; // sim/components.hpp

// Pure helper, unit-tested: is `target` inside the melee swing?
bool in_melee_arc(glm::vec2 origin, float yaw, float range, float arc_deg, glm::vec2 target);

// Spawns a projectile entity. Reused by player weapons and ranged enemy AI;
// `src_def` attributes player-damage telemetry to the firing enemy (0xffff = player).
entt::entity spawn_projectile(World& world, Team team, uint16_t weapon, uint16_t src_def,
                              glm::vec2 pos, glm::vec2 dir, float speed, float damage, float radius,
                              float ttl);

// Player weapon activation (sword swing, bolt spawn) for this tick's cmd.
void player_combat(World& world, const PlayerCmd& cmd, float dt);

// Moves projectiles, collides them with walls and enemies, applies damage.
void projectiles_update(World& world, float dt);

// Damage application + death + score + telemetry. Safe to call mid-iteration;
// destruction is deferred to cleanup_dead.
void damage_enemy(World& world, entt::entity enemy, float amount, int weapon_idx);

// Player damage with god-mode, hurt flash, telemetry and death handling.
// Called by enemy attacks (enemy_ai) and anything hazardous later.
void damage_player(World& world, float amount, uint16_t src_def);

// Removes expired and killed entities. Runs at the end of the tick.
void cleanup_dead(World& world, float dt);

} // namespace ds
