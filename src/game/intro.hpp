#pragma once

#include "sim/dungeon_gen.hpp"

namespace ds {

// The one-time opening beat: a canyon path to the cave mouth, guarded by a
// couple of walkers. The cave mouth is the map's exit_pos, so the sim's
// existing stairs flag (floor_exit_requested) is the binding trigger — the
// app intercepts it during the Intro phase instead of descending.
DungeonResult intro_approach_map();

} // namespace ds
