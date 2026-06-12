#include <doctest/doctest.h>

#include "sim/content.hpp"
#include "sim/telemetry.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace ds;

namespace {

ContentDB tiny_content() {
    ContentDB db;
    db.load_enemies_from_string(R"({"walker": {"hp": 12, "sprite": "monster"}})");
    db.load_weapons_from_string(
        R"({"sword": {"type": "melee", "damage": 7}, "bolt": {"type": "projectile", "damage": 5}})");
    return db;
}

TelemetryEvent make_event(uint32_t tick, EvType type) {
    TelemetryEvent ev;
    ev.tick = tick;
    ev.type = type;
    return ev;
}

} // namespace

TEST_CASE("ring overflow keeps the newest events") {
    TelemetryRecorder rec;
    rec.begin_run(1);
    const auto n = static_cast<uint32_t>(TelemetryRecorder::kCapacity + 100);
    for (uint32_t i = 0; i < n; ++i) {
        rec.record(make_event(i, EvType::PlayerMoveSample));
    }
    CHECK(rec.total() == n);
    CHECK(rec.size() == TelemetryRecorder::kCapacity);
    CHECK(rec.events().front().tick == n - TelemetryRecorder::kCapacity);
    CHECK(rec.events().back().tick == n - 1);
}

TEST_CASE("drain_since returns only new events and advances the cursor") {
    TelemetryRecorder rec;
    rec.begin_run(1);
    rec.record(make_event(0, EvType::RunStart));
    rec.record(make_event(1, EvType::PlayerDash));

    uint64_t cursor = 0;
    std::vector<TelemetryEvent> out;
    rec.drain_since(cursor, out);
    CHECK(out.size() == 2);
    CHECK(cursor == 2);

    rec.drain_since(cursor, out);
    CHECK(out.empty());

    rec.record(make_event(2, EvType::EnemyKilled));
    rec.drain_since(cursor, out);
    CHECK(out.size() == 1);
    CHECK(out[0].type == EvType::EnemyKilled);
}

TEST_CASE("written JSON round-trips with version, meta and event fields") {
    const ContentDB db = tiny_content();
    // Note: nlohmann::json sorts object keys, so def order is alphabetical —
    // always resolve indices by id.
    const int sword = db.find_weapon("sword");
    const int bolt = db.find_weapon("bolt");
    REQUIRE(sword >= 0);
    REQUIRE(bolt >= 0);
    TelemetryRecorder rec;
    rec.begin_run(42);

    rec.record(make_event(0, EvType::RunStart));

    TelemetryEvent attack = make_event(120, EvType::PlayerAttack);
    attack.flags = static_cast<uint8_t>(1u | (static_cast<unsigned>(sword) << 1)); // hit + weapon
    attack.def = 0;   // walker
    attack.a = 7.0f;
    attack.x = 12.5f;
    attack.y = 4.25f;
    attack.yaw = 1.5f;
    rec.record(attack);

    TelemetryEvent kill = make_event(180, EvType::EnemyKilled);
    kill.def = 0;
    kill.a = static_cast<float>(bolt);
    kill.b = 3.0f;
    rec.record(kill);

    const auto dir = std::filesystem::temp_directory_path() / "ds_telemetry_test";
    std::filesystem::remove_all(dir);
    const auto path = rec.write_json(dir, db, "death", /*cheats_used=*/true);
    REQUIRE(!path.empty());
    REQUIRE(std::filesystem::exists(path));

    std::ifstream f(path);
    const nlohmann::json doc = nlohmann::json::parse(f);
    CHECK(doc["version"] == 1);
    CHECK(doc["seed"] == 42);
    CHECK(doc["outcome"] == "death");
    CHECK(doc["cheats_used"] == true);
    REQUIRE(doc["events"].size() == 3);

    const auto& a = doc["events"][1];
    CHECK(a["type"] == "player_attack");
    CHECK(a["weapon"] == "sword");
    CHECK(a["hit"] == true);
    CHECK(a["target"] == "walker");
    CHECK(a["dmg"] == 7.0f);
    CHECK(a["pos"][0] == 12.5f);

    const auto& k = doc["events"][2];
    CHECK(k["type"] == "enemy_killed");
    CHECK(k["weapon"] == "bolt");
    CHECK(k["alive_s"] == 3.0f);

    std::filesystem::remove_all(dir);
}

TEST_CASE("begin_run resets state") {
    TelemetryRecorder rec;
    rec.begin_run(1);
    rec.record(make_event(0, EvType::RunStart));
    CHECK(rec.total() == 1);
    rec.begin_run(2);
    CHECK(rec.total() == 0);
    CHECK(rec.size() == 0);
}
