#pragma once

#include "sim/boss.hpp"
#include "sim/telemetry.hpp"

#include <map>
#include <span>
#include <string>

namespace ds {

struct ContentDB;

// How the player fights, extracted from a telemetry window (a whole run, or
// one boss fight). Pure function of the event stream — deterministic and
// unit-testable, which keeps the "AI" honest.
struct FightStyle {
    float melee_ratio = 0.5f; // 1 = all blade, 0 = all bolt
    float mobility = 0.5f;    // 1 = kiting sprinter, 0 = holds ground
    int sample_events = 0;    // how much signal the window carried
};

FightStyle extract_style(std::span<const TelemetryEvent> events, uint32_t from_tick,
                         uint32_t to_tick, const ContentDB& content);

// The wyrm's memory of ONE hero (per class id): a weight per counter-tag.
// Long-term state lives in the profile; a per-run overlay learns faster from
// each floor-boss fight, exactly as the design asks: patterns across runs in
// the long term, this run's habits in the short term.
struct BrainState {
    BossTagWeights weights{1.0f, 1.0f, 1.0f, 1.0f};
    int observed = 0; // runs (long-term) or boss fights (overlay)
};

// The style a window showed -> the tag weights that punish it.
BossTagWeights style_to_desire(const FightStyle& style);

// Long-term update after a finished run; deeper runs teach the wyrm more.
void brain_update_long(BrainState& brain, const FightStyle& run_style, int floor_reached);

// Short-term update after one boss fight within the current run.
void brain_update_short(BrainState& overlay, const FightStyle& fight_style);

// Effective pattern-weight multipliers: long-term memory x this run's
// overlay, clamped so no pattern ever fully vanishes or dominates.
BossTagWeights brain_tag_weights(const BrainState& long_term, const BrainState& overlay);

// Profile-blob (de)serialization; forgiving on garbage (returns empty map).
std::string brain_to_json(const std::map<std::string, BrainState>& per_class);
std::map<std::string, BrainState> brain_from_json(const std::string& json_text);

} // namespace ds
