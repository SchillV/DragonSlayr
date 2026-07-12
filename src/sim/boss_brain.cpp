#include "sim/boss_brain.hpp"

#include "sim/content.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

constexpr float kWeightFloor = 0.4f; // memory never erases a pattern...
constexpr float kWeightCeil = 2.2f;  // ...nor fixates on one
constexpr float kCombinedFloor = 0.3f;
constexpr float kCombinedCeil = 3.0f;
constexpr int kMinSampleEvents = 8; // windows thinner than this teach nothing

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

void ewma_toward(BrainState& brain, const BossTagWeights& desired, float alpha) {
    for (size_t i = 0; i < kCounterTagCount; ++i) {
        brain.weights[i] =
            std::clamp(lerp(brain.weights[i], desired[i], alpha), kWeightFloor, kWeightCeil);
    }
    ++brain.observed;
}

} // namespace

FightStyle extract_style(std::span<const TelemetryEvent> events, uint32_t from_tick,
                         uint32_t to_tick, const ContentDB& content) {
    FightStyle style;
    int melee_attacks = 0;
    int bolt_attacks = 0;
    int melee_kills = 0;
    int bolt_kills = 0;
    int dashes = 0;
    float speed_sum = 0.0f;
    int speed_samples = 0;

    for (const TelemetryEvent& ev : events) {
        if (ev.tick < from_tick || ev.tick > to_tick) {
            continue;
        }
        ++style.sample_events;
        switch (ev.type) {
        case EvType::PlayerAttack: ++melee_attacks; break;
        case EvType::ProjectileFired: ++bolt_attacks; break;
        case EvType::EnemyKilled: {
            const int weapon = static_cast<int>(ev.a);
            if (weapon >= 0 && static_cast<size_t>(weapon) < content.weapons.size() &&
                content.weapons[static_cast<size_t>(weapon)].type == WeaponDef::Type::Melee) {
                ++melee_kills;
            } else {
                ++bolt_kills;
            }
            break;
        }
        case EvType::PlayerDash: ++dashes; break;
        case EvType::PlayerMoveSample:
            speed_sum += std::sqrt(ev.a * ev.a + ev.b * ev.b);
            ++speed_samples;
            break;
        default: break;
        }
    }

    // Kills are the strongest intent signal; attack mix fills in when a
    // window had no kills (e.g. a fight the player lost).
    if (melee_kills + bolt_kills > 0) {
        style.melee_ratio = static_cast<float>(melee_kills) /
                            static_cast<float>(melee_kills + bolt_kills);
    } else if (melee_attacks + bolt_attacks > 0) {
        style.melee_ratio = static_cast<float>(melee_attacks) /
                            static_cast<float>(melee_attacks + bolt_attacks);
    }

    // Mobility: average speed against the ~7.5 tiles/s sprint, boosted by
    // dash usage (dashes per minute of window).
    const float minutes =
        std::max(static_cast<float>(to_tick - from_tick) / (60.0f * 60.0f), 1.0f / 60.0f);
    const float speed_norm =
        speed_samples > 0 ? std::clamp(speed_sum / static_cast<float>(speed_samples) / 7.5f,
                                       0.0f, 1.0f)
                          : 0.5f;
    const float dash_norm = std::clamp(static_cast<float>(dashes) / minutes / 12.0f, 0.0f, 1.0f);
    style.mobility = std::clamp(0.65f * speed_norm + 0.35f * dash_norm, 0.0f, 1.0f);
    return style;
}

BossTagWeights style_to_desire(const FightStyle& style) {
    BossTagWeights desired;
    desired[static_cast<size_t>(CounterTag::Melee)] = 0.5f + style.melee_ratio;
    desired[static_cast<size_t>(CounterTag::Ranged)] = 0.5f + (1.0f - style.melee_ratio);
    desired[static_cast<size_t>(CounterTag::Kite)] = 0.5f + style.mobility;
    desired[static_cast<size_t>(CounterTag::Turtle)] = 0.5f + (1.0f - style.mobility);
    return desired;
}

void brain_update_long(BrainState& brain, const FightStyle& run_style, int floor_reached) {
    if (run_style.sample_events < kMinSampleEvents) {
        return; // a run that barely happened teaches nothing
    }
    // Deeper runs weigh more: the wyrm studies the heroes that hurt it.
    const float alpha =
        0.22f + 0.18f * std::min(static_cast<float>(floor_reached) / 6.0f, 1.0f);
    ewma_toward(brain, style_to_desire(run_style), alpha);
}

void brain_update_short(BrainState& overlay, const FightStyle& fight_style) {
    if (fight_style.sample_events < kMinSampleEvents) {
        return;
    }
    ewma_toward(overlay, style_to_desire(fight_style), 0.55f);
}

BossTagWeights brain_tag_weights(const BrainState& long_term, const BrainState& overlay) {
    BossTagWeights out;
    for (size_t i = 0; i < kCounterTagCount; ++i) {
        out[i] = std::clamp(long_term.weights[i] * overlay.weights[i], kCombinedFloor,
                            kCombinedCeil);
    }
    return out;
}

std::string brain_to_json(const std::map<std::string, BrainState>& per_class) {
    nlohmann::json doc;
    for (const auto& [class_id, brain] : per_class) {
        doc[class_id] = {{"w", brain.weights}, {"runs", brain.observed}};
    }
    return doc.dump();
}

std::map<std::string, BrainState> brain_from_json(const std::string& json_text) {
    std::map<std::string, BrainState> out;
    const nlohmann::json doc =
        nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return out;
    }
    for (const auto& [class_id, entry] : doc.items()) {
        if (!entry.is_object() || !entry.contains("w") || !entry["w"].is_array() ||
            entry["w"].size() != kCounterTagCount) {
            continue;
        }
        BrainState brain;
        for (size_t i = 0; i < kCounterTagCount; ++i) {
            if (entry["w"][i].is_number()) {
                brain.weights[i] = std::clamp(entry["w"][i].get<float>(), kWeightFloor,
                                              kWeightCeil);
            }
        }
        brain.observed = entry.value("runs", 0);
        out[class_id] = brain;
    }
    return out;
}

} // namespace ds
