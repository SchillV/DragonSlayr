#include <doctest/doctest.h>

#include "game/profile.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

#include <filesystem>
#include <fstream>

using namespace ds;
using doctest::Approx;

namespace {

constexpr const char* kUpgrades = R"({
  "tough_hide": {
    "name": "Tough Hide", "max_ranks": 3, "cost": 100, "cost_per_rank": 50,
    "modifiers": [ { "stat": "max_hp", "op": "add", "value": 10 } ]
  },
  "keen_edge": {
    "max_ranks": 2, "cost": 100, "cost_per_rank": 50,
    "modifiers": [ { "stat": "melee_damage_mult", "op": "mult", "value": 1.05 } ]
  }
})";

std::filesystem::path scratch_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "ds_profile_tests";
    std::filesystem::remove_all(dir);
    return dir;
}

World tiny_world() {
    World world;
    REQUIRE(world.content.load_upgrades_from_string(kUpgrades));
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    d.rooms.push_back({1, 1, 6, 6});
    d.player_spawn = {3, 3};
    world.init_from_dungeon(std::move(d), 61);
    return world;
}

} // namespace

TEST_CASE("profiles round-trip everything") {
    const auto dir = scratch_dir();

    Profile p;
    p.slot = 2;
    p.class_id = "ranger";
    p.intro_done = true;
    p.embers = 725;
    p.upgrades = {{"tough_hide", 3}, {"keen_edge", 1}};
    p.records = {14, 7, 1340, 220};
    p.brain = R"({"anti_melee":0.7})";
    REQUIRE(save_profile(dir, p));

    Profile q;
    REQUIRE(load_profile(dir, 2, q));
    CHECK(q.loaded);
    CHECK(q.slot == 2);
    CHECK(q.class_id == "ranger");
    CHECK(q.intro_done);
    CHECK(q.embers == 725);
    CHECK(q.upgrades.at("tough_hide") == 3);
    CHECK(q.upgrades.at("keen_edge") == 1);
    CHECK(q.records.runs == 14);
    CHECK(q.records.best_floor == 7);
    CHECK(q.records.best_score == 1340);
    CHECK(q.records.kills == 220);
    CHECK(q.brain.find("anti_melee") != std::string::npos);
}

TEST_CASE("missing and corrupt slots load gracefully") {
    const auto dir = scratch_dir();
    Profile p;
    std::string err;
    CHECK_FALSE(load_profile(dir, 1, p, &err));
    CHECK_FALSE(err.empty());

    std::filesystem::create_directories(dir);
    std::ofstream(profile_path(dir, 1)) << "not json {";
    CHECK_FALSE(load_profile(dir, 1, p, &err));
    CHECK(err.find("not valid") != std::string::npos);
}

TEST_CASE("slot listing summarizes occupied slots") {
    const auto dir = scratch_dir();
    Profile p;
    p.slot = 2;
    p.class_id = "knight";
    p.records.runs = 3;
    p.records.best_floor = 4;
    p.records.best_score = 500;
    REQUIRE(save_profile(dir, p));

    const auto slots = list_profiles(dir);
    REQUIRE(slots.size() == static_cast<size_t>(kProfileSlots));
    CHECK_FALSE(slots[0].exists);
    CHECK(slots[1].exists);
    CHECK(slots[1].line.find("KNIGHT") != std::string::npos);
    CHECK(slots[1].line.find("3 RUNS") != std::string::npos);
    CHECK(slots[1].line.find("FLOOR 4") != std::string::npos);
    CHECK_FALSE(slots[2].exists);

    CHECK(delete_profile(dir, 2));
    CHECK_FALSE(list_profiles(dir)[1].exists);
}

TEST_CASE("embers scale with score and depth") {
    CHECK(embers_for_run(0, 1) == 0);
    CHECK(embers_for_run(500, 1) == 50);
    CHECK(embers_for_run(500, 4) == 50 + 75);
}

TEST_CASE("meta upgrades land as kMetaSource modifiers, capped and idempotent") {
    World world = tiny_world();
    Profile p;
    p.loaded = true;
    p.upgrades = {{"tough_hide", 2}, {"keen_edge", 99}, {"ghost_upgrade", 5}};

    apply_meta_upgrades(world, p);
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.max_hp == Approx(120.0f));                    // 2 ranks * +10
    CHECK(stats.cached.melee_damage_mult == Approx(1.05f * 1.05f)); // capped at max_ranks 2
    CHECK(world.reg.get<Health>(world.player).hp == Approx(120.0f)); // starts whole

    apply_meta_upgrades(world, p); // re-application replaces, never stacks
    CHECK(world.reg.get<StatBlock>(world.player).cached.max_hp == Approx(120.0f));
}
