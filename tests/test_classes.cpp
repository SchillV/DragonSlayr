#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/feats.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

using namespace ds;
using doctest::Approx;

namespace {

constexpr const char* kWeapons = R"({
  "sword": {"type": "melee", "damage": 7, "range": 1.6, "arc_deg": 90, "cooldown_s": 0.45},
  "bolt": {"type": "projectile", "damage": 5, "speed": 14.0, "radius": 0.1, "ttl_s": 3.0},
  "greatsword": {"type": "melee", "damage": 11, "range": 1.8, "arc_deg": 70, "cooldown_s": 0.7}})";
constexpr const char* kFeats = R"({
  "last_stand": {
    "hooks": [ { "on": "on_damaged", "effect": "temp_stat",
                 "stat": "defense_pct", "op": "add", "value": 0.2, "duration_s": 2.0 } ] },
  "keen_eye": { "modifiers": [ { "stat": "dex", "op": "add", "value": 2 } ] }
})";
constexpr const char* kClasses = R"({
  "knight": {
    "name": "Knight",
    "desc": "Steel and stubbornness.",
    "str": 2, "vit": 3,
    "feats": ["last_stand"],
    "primary": "greatsword"
  },
  "ranger": {
    "dex": 4,
    "feats": ["keen_eye", "ghost_feat_not_defined"]
  }
})";

DungeonResult flat_map() {
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(12, 12, Tile::Floor);
    d.rooms.push_back({1, 1, 10, 10});
    d.player_spawn = {3, 3};
    d.exit_pos = {10, 10};
    return d;
}

World make_world(int class_index) {
    World world;
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_feats_from_string(kFeats));
    REQUIRE(world.content.load_classes_from_string(kClasses));
    world.selected_class = class_index;
    world.init_from_dungeon(flat_map(), 41);
    return world;
}

} // namespace

TEST_CASE("class defs parse attributes, feats and loadout") {
    ContentDB db;
    REQUIRE(db.load_classes_from_string(kClasses));
    REQUIRE(db.classes.size() == 2);

    const ClassDef& knight = db.classes[static_cast<size_t>(db.find_class("knight"))];
    CHECK(knight.name == "Knight");
    CHECK(knight.str == 2);
    CHECK(knight.vit == 3);
    CHECK(knight.dex == 0);
    REQUIRE(knight.feats.size() == 1);
    CHECK(knight.feats[0] == "last_stand");
    CHECK(knight.primary == "greatsword");
    CHECK(knight.secondary == "bolt"); // default loadout slot

    const ClassDef& ranger = db.classes[static_cast<size_t>(db.find_class("ranger"))];
    CHECK(ranger.name == "ranger"); // name defaults to id
}

TEST_CASE("a fresh run starts as the selected class") {
    World world = make_world(0); // knight (alphabetical: knight, ranger)

    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.str == Approx(2.0f));
    CHECK(stats.cached.vit == Approx(3.0f));
    CHECK(stats.cached.melee_damage_mult == Approx(1.1f)); // via STR derivation
    // Health seeded from the class package, at full.
    const auto& hp = world.reg.get<Health>(world.player);
    CHECK(hp.max_hp == Approx(124.0f)); // 100 + 3 vit * 8
    CHECK(hp.hp == Approx(124.0f));
    // Starting feat + class loadout.
    CHECK(feat_stacks(world, world.content.find_feat("last_stand")) == 1);
    CHECK(world.primary_weapon == world.content.find_weapon("greatsword"));
    CHECK(world.secondary_weapon == world.content.find_weapon("bolt"));
}

TEST_CASE("unknown feats in a class are skipped without dying") {
    World world = make_world(1); // ranger references a ghost feat
    CHECK(world.reg.get<StatBlock>(world.player).cached.dex == Approx(6.0f)); // 4 class + 2 keen_eye
    CHECK(feat_stacks(world, world.content.find_feat("keen_eye")) == 1);
}

TEST_CASE("restarting re-applies the class from a clean slate") {
    World world = make_world(0);
    // Mid-run growth: an extra feat stack and some damage.
    grant_feat(world, world.content.find_feat("keen_eye"));
    world.reg.get<Health>(world.player).hp = 30.0f;

    world.init_from_dungeon(flat_map(), 42); // new run, same class
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.dex == Approx(0.0f)); // keen_eye gone
    CHECK(stats.cached.vit == Approx(3.0f)); // class package back, exactly once
    CHECK(world.reg.get<Health>(world.player).hp == Approx(124.0f));
    CHECK(feat_stacks(world, world.content.find_feat("keen_eye")) == 0);
    CHECK(feat_stacks(world, world.content.find_feat("last_stand")) == 1);
}

TEST_CASE("class package survives the stairs like everything else") {
    World world = make_world(0);
    world.advance_floor(flat_map());
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.vit == Approx(3.0f));
    CHECK(feat_stacks(world, world.content.find_feat("last_stand")) == 1);
    CHECK(world.primary_weapon == world.content.find_weapon("greatsword"));
}

TEST_CASE("no classes defined still boots with the default loadout") {
    World world;
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    world.selected_class = 0; // out of range for an empty roster
    world.init_from_dungeon(flat_map(), 43);
    CHECK(world.primary_weapon == world.content.find_weapon("sword"));
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(100.0f));
}
