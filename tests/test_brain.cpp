#include <doctest/doctest.h>

#include "sim/boss_brain.hpp"
#include "sim/content.hpp"

using namespace ds;
using doctest::Approx;

namespace {

constexpr const char* kWeapons = R"({
  "sword": {"type": "melee", "damage": 7},
  "bolt": {"type": "projectile", "damage": 5}})";

ContentDB weapons_db() {
    ContentDB db;
    REQUIRE(db.load_weapons_from_string(kWeapons));
    return db;
}

TelemetryEvent ev(EvType type, uint32_t tick, float a = 0.0f, float b = 0.0f) {
    TelemetryEvent e;
    e.type = type;
    e.tick = tick;
    e.a = a;
    e.b = b;
    return e;
}

size_t tag(CounterTag t) {
    return static_cast<size_t>(t);
}

} // namespace

TEST_CASE("style extraction reads the blade from the stream") {
    const ContentDB db = weapons_db();
    const auto sword = static_cast<float>(db.find_weapon("sword"));
    std::vector<TelemetryEvent> events;
    for (uint32_t i = 0; i < 12; ++i) {
        events.push_back(ev(EvType::PlayerAttack, i * 30));
        events.push_back(ev(EvType::EnemyKilled, i * 30 + 5, /*weapon=*/sword));
    }
    // Slow feet: crawling move samples, no dashes.
    for (uint32_t i = 0; i < 10; ++i) {
        events.push_back(ev(EvType::PlayerMoveSample, i * 40, 0.5f, 0.0f));
    }

    const FightStyle style = extract_style(events, 0, 600, db);
    CHECK(style.melee_ratio > 0.9f);
    CHECK(style.mobility < 0.3f);
    CHECK(style.sample_events > 20);
}

TEST_CASE("style extraction reads bolts and kiting") {
    const ContentDB db = weapons_db();
    const auto bolt = static_cast<float>(db.find_weapon("bolt"));
    std::vector<TelemetryEvent> events;
    for (uint32_t i = 0; i < 12; ++i) {
        events.push_back(ev(EvType::ProjectileFired, i * 30));
        events.push_back(ev(EvType::EnemyKilled, i * 30 + 5, /*weapon=*/bolt));
    }
    for (uint32_t i = 0; i < 10; ++i) {
        events.push_back(ev(EvType::PlayerMoveSample, i * 40, 7.5f, 0.0f)); // sprinting
    }
    for (uint32_t i = 0; i < 8; ++i) {
        events.push_back(ev(EvType::PlayerDash, i * 45));
    }

    const FightStyle style = extract_style(events, 0, 600, db);
    CHECK(style.melee_ratio < 0.1f);
    CHECK(style.mobility > 0.7f);

    // The window filter holds: nothing outside [from, to].
    const FightStyle empty = extract_style(events, 5000, 6000, db);
    CHECK(empty.sample_events == 0);
}

TEST_CASE("desire punishes what the style leans on") {
    FightStyle melee_turtle;
    melee_turtle.melee_ratio = 1.0f;
    melee_turtle.mobility = 0.0f;
    const BossTagWeights d = style_to_desire(melee_turtle);
    CHECK(d[tag(CounterTag::Melee)] == Approx(1.5f));
    CHECK(d[tag(CounterTag::Ranged)] == Approx(0.5f));
    CHECK(d[tag(CounterTag::Turtle)] == Approx(1.5f));
    CHECK(d[tag(CounterTag::Kite)] == Approx(0.5f));
}

TEST_CASE("long-term learning drifts, deeper runs teach more, thin runs none") {
    FightStyle melee;
    melee.melee_ratio = 1.0f;
    melee.mobility = 0.5f;
    melee.sample_events = 100;

    BrainState shallow;
    brain_update_long(shallow, melee, 1);
    BrainState deep;
    brain_update_long(deep, melee, 9);
    CHECK(shallow.weights[tag(CounterTag::Melee)] > 1.0f);
    CHECK(deep.weights[tag(CounterTag::Melee)] > shallow.weights[tag(CounterTag::Melee)]);

    BrainState untouched;
    FightStyle thin = melee;
    thin.sample_events = 2;
    brain_update_long(untouched, thin, 9);
    CHECK(untouched.weights[tag(CounterTag::Melee)] == Approx(1.0f));
    CHECK(untouched.observed == 0);

    // Repeated lessons converge toward the desire but never past the clamps.
    BrainState converged;
    for (int i = 0; i < 50; ++i) {
        brain_update_long(converged, melee, 9);
    }
    CHECK(converged.weights[tag(CounterTag::Melee)] == Approx(1.5f).epsilon(0.02));
    CHECK(converged.weights[tag(CounterTag::Ranged)] == Approx(0.5f).epsilon(0.05));
    CHECK(converged.observed == 50);
}

TEST_CASE("the run overlay compounds with long-term memory") {
    FightStyle melee;
    melee.melee_ratio = 1.0f;
    melee.mobility = 0.5f;
    melee.sample_events = 50;

    BrainState long_term;
    for (int i = 0; i < 10; ++i) {
        brain_update_long(long_term, melee, 6);
    }
    BrainState overlay;
    brain_update_short(overlay, melee);

    const BossTagWeights w = brain_tag_weights(long_term, overlay);
    CHECK(w[tag(CounterTag::Melee)] >
          long_term.weights[tag(CounterTag::Melee)]); // overlay sharpens it
    CHECK(w[tag(CounterTag::Melee)] <= 3.0f);
    CHECK(w[tag(CounterTag::Ranged)] >= 0.3f);
}

TEST_CASE("brains serialize per class and survive garbage") {
    std::map<std::string, BrainState> brains;
    brains["knight"].weights = {1.4f, 0.6f, 1.0f, 1.2f};
    brains["knight"].observed = 7;
    brains["mage"].weights = {0.5f, 1.8f, 1.1f, 0.9f};

    const std::string blob = brain_to_json(brains);
    const auto parsed = brain_from_json(blob);
    REQUIRE(parsed.size() == 2);
    CHECK(parsed.at("knight").weights[0] == Approx(1.4f));
    CHECK(parsed.at("knight").observed == 7);
    CHECK(parsed.at("mage").weights[1] == Approx(1.8f));

    CHECK(brain_from_json("not json {").empty());
    CHECK(brain_from_json(R"({"knight": {"w": [1, 2]}})").empty()); // wrong arity
}

TEST_CASE("learning is deterministic") {
    const ContentDB db = weapons_db();
    std::vector<TelemetryEvent> events;
    for (uint32_t i = 0; i < 30; ++i) {
        events.push_back(ev(EvType::PlayerAttack, i * 20));
        events.push_back(ev(EvType::PlayerMoveSample, i * 20 + 3, 4.0f, 2.0f));
    }
    BrainState a, b;
    brain_update_long(a, extract_style(events, 0, 1000, db), 4);
    brain_update_long(b, extract_style(events, 0, 1000, db), 4);
    for (size_t i = 0; i < kCounterTagCount; ++i) {
        CHECK(a.weights[i] == Approx(b.weights[i]));
    }
}
