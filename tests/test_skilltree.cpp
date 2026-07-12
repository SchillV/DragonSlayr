#include <doctest/doctest.h>

#include "core/cvar.hpp"
#include "game/skill_tree_ui.hpp"
#include "render/font.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/feats.hpp"
#include "sim/progression.hpp"
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
  "berserk": { "hooks": [ { "on": "on_kill", "effect": "temp_stat",
               "stat": "melee_damage_mult", "op": "mult", "value": 1.2, "duration_s": 1 } ] }
})";
constexpr const char* kTrees = R"({
  "twig": {
    "name": "The Twig",
    "nodes": {
      "a": { "attr": "str", "points": 2 },
      "b": { "feat": "berserk", "requires": ["a"] },
      "c": { "attr": "vit", "cost": 2, "requires": ["b"] },
      "side": { "attr": "dex" }
    }
  }
})";
constexpr const char* kClasses = R"({ "knight": { "tree": "twig" } })";

World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_feats_from_string(kFeats));
    REQUIRE(world.content.load_skill_trees_from_string(kTrees));
    REQUIRE(world.content.load_classes_from_string(kClasses));

    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(12, 12, Tile::Floor);
    d.rooms.push_back({1, 1, 10, 10});
    d.player_spawn = {3, 3};
    d.exit_pos = {10, 10};
    world.selected_class = 0;
    world.init_from_dungeon(std::move(d), 51);
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

// Simple linear curve for predictable numbers; restores defaults after.
struct CurveOverride {
    float base, curve;
    CurveOverride(float b, float c) {
        base = cvar_find("sv.xp_base")->value;
        curve = cvar_find("sv.xp_curve")->value;
        cvar_set(*cvar_find("sv.xp_base"), b);
        cvar_set(*cvar_find("sv.xp_curve"), c);
    }
    ~CurveOverride() {
        cvar_set(*cvar_find("sv.xp_base"), base);
        cvar_set(*cvar_find("sv.xp_curve"), curve);
    }
};

} // namespace

TEST_CASE("tree validation rejects broken data with key paths") {
    ContentDB db;
    REQUIRE(db.load_feats_from_string(kFeats));
    std::string err;

    CHECK_FALSE(db.load_skill_trees_from_string(
        R"({"t": {"nodes": {"a": {"attr": "str", "requires": ["ghost"]}}}})", &err));
    CHECK(err.find("requires unknown node 'ghost'") != std::string::npos);

    CHECK_FALSE(db.load_skill_trees_from_string(
        R"({"t": {"nodes": {"a": {"attr": "str", "requires": ["b"]},
                            "b": {"attr": "vit", "requires": ["a"]}}}})",
        &err));
    CHECK(err.find("cycle") != std::string::npos);

    CHECK_FALSE(db.load_skill_trees_from_string(
        R"({"t": {"nodes": {"a": {"feat": "not_a_feat"}}}})", &err));
    CHECK(err.find("unknown feat 'not_a_feat'") != std::string::npos);

    CHECK_FALSE(db.load_skill_trees_from_string(
        R"({"t": {"nodes": {"a": {"feat": "berserk", "attr": "str"}}}})", &err));
    CHECK(err.find("exactly one") != std::string::npos);

    CHECK_FALSE(db.load_skill_trees_from_string(R"({"t": {"nodes": {"a": {}}}})", &err));
    CHECK(err.find("exactly one") != std::string::npos);

    CHECK(db.skill_trees.empty()); // nothing survived a failed load
}

TEST_CASE("xp awards level up on the curve and grant points") {
    const CurveOverride curve(100.0f, 1.0f); // thresholds: 100, 200, 300...
    World world = make_world();

    award_xp(world, 50.0f);
    CHECK(world.level == 1);
    CHECK(world.skill_points == 0);

    award_xp(world, 300.0f); // 350 total: 100 (lv2) + 200 (lv3) + 50 remainder
    CHECK(world.level == 3);
    CHECK(world.skill_points == 2);
    CHECK(world.xp == Approx(50.0f));

    int level_ups = 0;
    for (const TelemetryEvent& ev : world.telem.events()) {
        level_ups += ev.type == EvType::LevelUp ? 1 : 0;
    }
    CHECK(level_ups == 2);
}

TEST_CASE("kills feed xp through the enemy def") {
    World world = make_world();
    // walker: no explicit xp -> derived score/5 = 10 at load.
    CHECK(world.content.enemies[0].xp == 10);
    damage_enemy(world, spawn_walker(world, {8.0f, 8.0f}), 1000.0f, 0);
    CHECK(world.xp == Approx(10.0f));
}

TEST_CASE("purchase gating: prereqs, cost, no double-buy") {
    World world = make_world();
    REQUIRE(world.active_tree == world.content.find_skill_tree("twig"));
    const SkillTreeDef& tree = world.content.skill_trees[static_cast<size_t>(world.active_tree)];
    const int a = tree.find_node("a");
    const int b = tree.find_node("b");
    const int c = tree.find_node("c");

    CHECK(node_state(world, a) == NodeState::Available);
    CHECK(node_state(world, b) == NodeState::Locked);
    CHECK_FALSE(purchase_node(world, a)); // no points yet

    world.skill_points = 3;
    CHECK(purchase_node(world, a));
    CHECK(world.skill_points == 2);
    CHECK(node_state(world, a) == NodeState::Purchased);
    CHECK(world.reg.get<StatBlock>(world.player).cached.str == Approx(2.0f));
    CHECK_FALSE(purchase_node(world, a)); // owned

    CHECK(node_state(world, b) == NodeState::Available); // unlocked by a
    CHECK(purchase_node(world, b));
    CHECK(feat_stacks(world, world.content.find_feat("berserk")) == 1);

    // c costs 2 but only 1 point remains.
    CHECK(node_state(world, c) == NodeState::Available);
    CHECK_FALSE(purchase_node(world, c));
    CHECK(world.skill_points == 1);
}

TEST_CASE("progression is per-run but survives the stairs") {
    World world = make_world();
    world.skill_points = 1;
    const SkillTreeDef& tree = world.content.skill_trees[static_cast<size_t>(world.active_tree)];
    REQUIRE(purchase_node(world, tree.find_node("a")));
    award_xp(world, 10.0f);
    world.level = 4;

    DungeonResult next;
    next.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    next.rooms.push_back({1, 1, 6, 6});
    next.player_spawn = {2, 2};
    world.advance_floor(std::move(next));
    CHECK(world.level == 4);
    CHECK(world.purchased_nodes.size() == 1);
    CHECK(world.reg.get<StatBlock>(world.player).cached.str == Approx(2.0f));

    DungeonResult fresh;
    fresh.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    fresh.rooms.push_back({1, 1, 6, 6});
    fresh.player_spawn = {2, 2};
    world.init_from_dungeon(std::move(fresh), 52);
    CHECK(world.level == 1);
    CHECK(world.xp == Approx(0.0f));
    CHECK(world.purchased_nodes.empty());
    CHECK(world.reg.get<StatBlock>(world.player).cached.str == Approx(0.0f));
}

TEST_CASE("auto-layout ranks nodes by prerequisite depth") {
    ContentDB db;
    REQUIRE(db.load_feats_from_string(kFeats));
    REQUIRE(db.load_skill_trees_from_string(kTrees));
    const SkillTreeDef& tree = db.skill_trees[0];

    const std::vector<NodeVis> layout = layout_tree(tree, {1280.0f, 720.0f});
    REQUIRE(layout.size() == 4);
    auto vis_of = [&](const char* id) {
        for (const NodeVis& v : layout) {
            if (tree.nodes[static_cast<size_t>(v.node)].id == id) {
                return v;
            }
        }
        REQUIRE(false);
        return layout[0];
    };
    const NodeVis a = vis_of("a"), b = vis_of("b"), c = vis_of("c"), side = vis_of("side");
    CHECK(a.rank == 0);
    CHECK(b.rank == 1);
    CHECK(c.rank == 2);
    CHECK(side.rank == 0);
    CHECK(a.pos.x < b.pos.x);
    CHECK(b.pos.x < c.pos.x);
    CHECK(a.pos.x == Approx(side.pos.x)); // same rank, same column...
    CHECK(a.pos.y != Approx(side.pos.y)); // ...different rows
}

TEST_CASE("the tree page navigates, purchases and renders") {
    World world = make_world();
    world.skill_points = 1;

    SkillTreeUi ui;
    ui.open(world, {1280.0f, 720.0f});
    REQUIRE(ui.active());
    REQUIRE(ui.nodes().size() == 4);

    // Walk selection onto node "a" (rank 0) and buy it with Enter.
    const SkillTreeDef& tree = world.content.skill_trees[static_cast<size_t>(world.active_tree)];
    MenuInput in;
    for (int guard = 0; guard < 8 &&
                        tree.nodes[static_cast<size_t>(ui.nodes()[static_cast<size_t>(ui.selection())].node)].id != "a";
         ++guard) {
        in = {};
        in.down = true;
        ui.update(world, in, {1280.0f, 720.0f});
    }
    in = {};
    in.select = true;
    ui.update(world, in, {1280.0f, 720.0f});
    CHECK(world.purchased_nodes.size() == 1);

    // Renders geometry: connectors + nodes + labels.
    const FontAtlas font = bake_builtin_font();
    FrameView view;
    ui.render(view, font, world);
    CHECK(view.overlay.size() > 6);
    CHECK(view.overlay_text.size() > 0);

    // Esc requests close.
    in = {};
    in.back = true;
    ui.update(world, in, {1280.0f, 720.0f});
    CHECK(ui.close_requested());
}
