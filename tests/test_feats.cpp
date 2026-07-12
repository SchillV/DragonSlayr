#include <doctest/doctest.h>

#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/feats.hpp"
#include "sim/items.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

using namespace ds;
using doctest::Approx;

namespace {

constexpr const char* kEnemies = R"({"walker": {"hp": 12, "speed": 3.0, "sprite": "m",
    "score": 50, "attack": {"damage": 6, "range": 0.9}}})";
constexpr const char* kWeapons = R"({
  "sword": {"type": "melee", "damage": 7, "range": 1.6, "arc_deg": 90, "cooldown_s": 0.45}})";
constexpr const char* kFeats = R"({
  "wyrm_blood": {
    "name": "Wyrm Blood",
    "max_stacks": 3,
    "modifiers": [ { "stat": "vit", "op": "add", "value": 2 } ]
  },
  "bloodlust": {
    "desc": "Kills quicken your step.",
    "max_stacks": 2,
    "hooks": [ { "on": "on_kill", "effect": "temp_stat",
                 "stat": "move_speed_mult", "op": "mult", "value": 1.2, "duration_s": 0.5 } ]
  },
  "packrat": {
    "max_stacks": 0,
    "modifiers": [ { "stat": "max_hp", "op": "add", "value": 1 } ]
  }
})";

World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_feats_from_string(kFeats));

    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(16, 16, Tile::Floor);
    for (int i = 0; i < 16; ++i) {
        d.map.tiles.at(i, 0) = Tile::Wall;
        d.map.tiles.at(i, 15) = Tile::Wall;
        d.map.tiles.at(0, i) = Tile::Wall;
        d.map.tiles.at(15, i) = Tile::Wall;
    }
    d.rooms.push_back({1, 1, 14, 14});
    d.player_spawn = {3, 3};
    d.exit_pos = {12, 12};
    world.init_from_dungeon(std::move(d), 31);
    return world;
}

entt::entity spawn_walker(World& world, glm::vec2 pos) {
    const entt::entity e = world.reg.create();
    world.reg.emplace<Transform>(e, pos, 0.0f);
    world.reg.emplace<PrevTransform>(e, pos, 0.0f);
    world.reg.emplace<Velocity>(e);
    world.reg.emplace<Body>(e, 0.35f);
    world.reg.emplace<Health>(e, 12.0f, 12.0f);
    Enemy enemy;
    enemy.def = 0;
    world.reg.emplace<Enemy>(e, std::move(enemy));
    return e;
}

} // namespace

TEST_CASE("feat defs parse: names, stacks, shared modifier/hook vocabulary") {
    const World world = make_world();
    const ContentDB& c = world.content;
    REQUIRE(c.feats.size() == 3);

    const FeatDef& wb = c.feats[static_cast<size_t>(c.find_feat("wyrm_blood"))];
    CHECK(wb.name == "Wyrm Blood");
    CHECK(wb.max_stacks == 3);
    REQUIRE(wb.modifiers.size() == 1);
    CHECK(wb.modifiers[0].stat == StatId::Vit);

    const FeatDef& bl = c.feats[static_cast<size_t>(c.find_feat("bloodlust"))];
    CHECK(bl.name == "bloodlust"); // defaults to id
    CHECK(bl.desc == "Kills quicken your step.");
    REQUIRE(bl.hooks.size() == 1);
    CHECK(bl.hooks[0].on == ItemHookDef::Trigger::OnKill);
}

TEST_CASE("bad feat data errors carry the key path") {
    ContentDB db;
    std::string err;
    CHECK_FALSE(db.load_feats_from_string(
        R"({"x": {"modifiers": [ {"stat": "luck", "op": "add", "value": 1} ]}})", &err));
    CHECK(err.find("feats.x.modifiers[0]") != std::string::npos);
    CHECK(db.feats.empty());
}

TEST_CASE("granting stacks modifiers per stack and caps at max_stacks") {
    World world = make_world();
    const int wb = world.content.find_feat("wyrm_blood");

    CHECK(grant_feat(world, wb) == 1);
    CHECK(world.reg.get<StatBlock>(world.player).cached.vit == Approx(2.0f));
    CHECK(grant_feat(world, wb) == 2);
    CHECK(grant_feat(world, wb) == 3);
    CHECK(world.reg.get<StatBlock>(world.player).cached.vit == Approx(6.0f));
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(148.0f)); // 100 + 6*8

    CHECK(grant_feat(world, wb) == 0); // capped
    CHECK(feat_stacks(world, wb) == 3);

    // max_stacks 0 = unlimited.
    const int pr = world.content.find_feat("packrat");
    for (int i = 1; i <= 7; ++i) {
        CHECK(grant_feat(world, pr) == i);
    }
}

TEST_CASE("feat hooks fire once per stack") {
    World world = make_world();
    const int bl = world.content.find_feat("bloodlust");
    grant_feat(world, bl);
    grant_feat(world, bl);

    damage_enemy(world, spawn_walker(world, {8.0f, 8.0f}), 1000.0f, 0);

    // Two stacks -> two temp modifiers -> 1.2^2 speed.
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.move_speed_mult == Approx(1.44f));
    CHECK(world.reg.get<TempMods>(world.player).entries.size() == 2);
}

TEST_CASE("remove_feat strips all stacks and their modifiers") {
    World world = make_world();
    const int wb = world.content.find_feat("wyrm_blood");
    grant_feat(world, wb);
    grant_feat(world, wb);
    remove_feat(world, wb);
    CHECK(feat_stacks(world, wb) == 0);
    CHECK(world.reg.get<StatBlock>(world.player).cached.vit == Approx(0.0f));
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(100.0f));
}

TEST_CASE("hot reload remaps feat stacks and drops removed feats") {
    World world = make_world();
    grant_feat(world, world.content.find_feat("wyrm_blood")); // vit +2
    grant_feat(world, world.content.find_feat("bloodlust"));

    // Reorder the roster and delete bloodlust; wyrm_blood shifts index.
    ContentDB fresh = world.content;
    REQUIRE(fresh.load_feats_from_string(R"({
      "aaa_first": { "modifiers": [ { "stat": "str", "op": "add", "value": 1 } ] },
      "wyrm_blood": { "max_stacks": 3, "modifiers": [ { "stat": "vit", "op": "add", "value": 2 } ] }
    })"));
    world.apply_content(std::move(fresh));

    const auto& set = world.reg.get<FeatSet>(world.player);
    REQUIRE(set.entries.size() == 1);
    CHECK(world.content.feats[set.entries[0].feat].id == "wyrm_blood");
    CHECK(world.reg.get<StatBlock>(world.player).cached.vit == Approx(2.0f)); // survived remap
    CHECK(world.reg.get<StatBlock>(world.player).cached.move_speed_mult == Approx(1.0f));
}

TEST_CASE("feats and their modifiers survive the stairs") {
    World world = make_world();
    const int wb = world.content.find_feat("wyrm_blood");
    grant_feat(world, wb);
    grant_feat(world, wb);

    DungeonResult next;
    next.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    next.rooms.push_back({1, 1, 6, 6});
    next.player_spawn = {2, 2};
    world.advance_floor(std::move(next));

    CHECK(feat_stacks(world, wb) == 2);
    CHECK(world.reg.get<StatBlock>(world.player).cached.vit == Approx(4.0f));
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(132.0f));
}

TEST_CASE("feat gains are recorded in telemetry") {
    World world = make_world();
    grant_feat(world, world.content.find_feat("wyrm_blood"));
    int gained = 0;
    for (const TelemetryEvent& ev : world.telem.events()) {
        gained += ev.type == EvType::FeatGained ? 1 : 0;
    }
    CHECK(gained == 1);
}
