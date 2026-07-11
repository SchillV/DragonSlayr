#include <doctest/doctest.h>

#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
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
constexpr const char* kItems = R"({
  "fury": {
    "sprite": "fury",
    "modifiers": [ { "stat": "damage_mult", "op": "mult", "value": 2.0 } ]
  },
  "heart": {
    "name": "Wyrm Heart",
    "sprite": "heart",
    "modifiers": [ { "stat": "max_hp", "op": "add", "value": 50 } ],
    "hooks": [ { "on": "on_kill", "effect": "heal", "amount": 5 } ]
  },
  "bloodlust": {
    "sprite": "bl",
    "hooks": [ { "on": "on_kill", "effect": "temp_stat",
                 "stat": "move_speed_mult", "op": "mult", "value": 1.5, "duration_s": 0.1 } ]
  },
  "sigil": {
    "sprite": "sg",
    "hooks": [ { "on": "on_hit", "effect": "aoe_damage", "amount": 3, "radius": 2.0 } ]
  }
})";

World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_items_from_string(kItems));

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
    world.init_from_dungeon(std::move(d), 11);
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

void kill_walker(World& world, entt::entity e) {
    damage_enemy(world, e, 1000.0f, 0);
}

} // namespace

TEST_CASE("item defs parse modifiers and hooks") {
    const World world = make_world();
    const ContentDB& c = world.content;
    REQUIRE(c.items.size() == 4);

    const ItemDef& heart = c.items[static_cast<size_t>(c.find_item("heart"))];
    CHECK(heart.name == "Wyrm Heart");
    REQUIRE(heart.modifiers.size() == 1);
    CHECK(heart.modifiers[0].stat == StatId::MaxHp);
    CHECK(heart.modifiers[0].op == Modifier::Op::Add);
    CHECK(heart.modifiers[0].value == Approx(50.0f));
    REQUIRE(heart.hooks.size() == 1);
    CHECK(heart.hooks[0].on == ItemHookDef::Trigger::OnKill);
    CHECK(heart.hooks[0].effect == ItemHookDef::Effect::Heal);
    CHECK(heart.hooks[0].amount == Approx(5.0f));

    const ItemDef& fury = c.items[static_cast<size_t>(c.find_item("fury"))];
    CHECK(fury.name == "fury"); // name defaults to id
}

TEST_CASE("bad item data errors carry the key path") {
    ContentDB db;
    std::string err;
    CHECK_FALSE(db.load_items_from_string(
        R"({"x": {"sprite": "s", "modifiers": [ {"stat": "swagger", "op": "add", "value": 1} ]}})",
        &err));
    CHECK(err.find("items.x.modifiers[0]") != std::string::npos);
    CHECK(err.find("swagger") != std::string::npos);

    CHECK_FALSE(db.load_items_from_string(
        R"({"x": {"sprite": "s", "hooks": [ {"on": "on_sneeze", "effect": "heal"} ]}})", &err));
    CHECK(err.find("items.x.hooks[0].on") != std::string::npos);
    CHECK(db.items.empty()); // failed loads leave the roster untouched
}

TEST_CASE("walking over a pickup grants it") {
    World world = make_world();
    const glm::vec2 p = world.reg.get<Transform>(world.player).pos;
    world.spawn_pickup(world.content.find_item("fury"), p + glm::vec2{0.3f, 0.0f});

    world.tick({}, 1.0f / 60.0f);

    const auto* inv = world.reg.try_get<Inventory>(world.player);
    REQUIRE(inv != nullptr);
    REQUIRE(inv->items.size() == 1);
    CHECK(world.content.items[inv->items[0]].id == "fury");
    CHECK(world.reg.get<StatBlock>(world.player).cached.damage_mult == Approx(2.0f));
    CHECK(world.reg.view<Pickup>().size() == 0); // consumed

    bool recorded = false;
    for (const TelemetryEvent& ev : world.telem.events()) {
        recorded |= ev.type == EvType::ItemPickup;
    }
    CHECK(recorded);
}

TEST_CASE("max_hp item raises health and heals by the gain") {
    World world = make_world();
    world.reg.get<Health>(world.player).hp = 40.0f;
    grant_item(world, world.content.find_item("heart"));

    const auto& hp = world.reg.get<Health>(world.player);
    CHECK(hp.max_hp == Approx(150.0f));
    CHECK(hp.hp == Approx(90.0f)); // 40 + the 50 gained
}

TEST_CASE("on_kill heal fires when an enemy dies") {
    World world = make_world();
    grant_item(world, world.content.find_item("heart")); // +50 max, on_kill heal 5
    world.reg.get<Health>(world.player).hp = 60.0f;

    kill_walker(world, spawn_walker(world, {8.0f, 8.0f}));
    CHECK(world.reg.get<Health>(world.player).hp == Approx(65.0f));
}

TEST_CASE("temp_stat applies for its duration then expires") {
    World world = make_world();
    grant_item(world, world.content.find_item("bloodlust"));
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.move_speed_mult == Approx(1.0f));

    kill_walker(world, spawn_walker(world, {8.0f, 8.0f}));
    CHECK(stats.cached.move_speed_mult == Approx(1.5f));

    for (int i = 0; i < 12; ++i) { // 0.2 s > the 0.1 s duration
        world.tick({}, 1.0f / 60.0f);
    }
    CHECK(stats.cached.move_speed_mult == Approx(1.0f));
    CHECK(world.reg.get<TempMods>(world.player).entries.empty());
}

TEST_CASE("on_hit aoe damages nearby enemies but chains only one level") {
    World world = make_world();
    grant_item(world, world.content.find_item("sigil")); // on_hit: 3 dmg in r=2

    const entt::entity struck = spawn_walker(world, {8.0f, 8.0f});
    const entt::entity bystander = spawn_walker(world, {9.0f, 8.0f});
    const entt::entity far_away = spawn_walker(world, {13.0f, 13.0f});

    damage_enemy(world, struck, 5.0f, 0);
    items_dispatch(world, ItemHookDef::Trigger::OnHit, {{8.0f, 8.0f}});

    CHECK(world.reg.get<Health>(struck).hp == Approx(4.0f));    // 12 - 5 - 3
    CHECK(world.reg.get<Health>(bystander).hp == Approx(9.0f)); // 12 - 3
    CHECK(world.reg.get<Health>(far_away).hp == Approx(12.0f)); // out of range
}

TEST_CASE("hot reload remaps held items and drops removed ones") {
    World world = make_world();
    grant_item(world, world.content.find_item("fury"));  // damage_mult x2
    grant_item(world, world.content.find_item("heart")); // +50 max hp
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(150.0f));

    // Reload with fury deleted and the roster reordered: heart survives with
    // a remapped index, fury's modifier disappears.
    ContentDB fresh = world.content;
    REQUIRE(fresh.load_items_from_string(R"({
      "zzz_pad": { "sprite": "p" },
      "heart": {
        "sprite": "heart",
        "modifiers": [ { "stat": "max_hp", "op": "add", "value": 50 } ]
      }
    })"));
    world.apply_content(std::move(fresh));

    const auto& inv = world.reg.get<Inventory>(world.player);
    REQUIRE(inv.items.size() == 1);
    CHECK(world.content.items[inv.items[0]].id == "heart");
    const auto& stats = world.reg.get<StatBlock>(world.player);
    CHECK(stats.cached.damage_mult == Approx(1.0f)); // fury gone
    CHECK(stats.cached.max_hp == Approx(150.0f));    // heart survived the remap
    CHECK(world.reg.get<Health>(world.player).max_hp == Approx(150.0f));
}

TEST_CASE("pickup entities remap or vanish on hot reload") {
    World world = make_world();
    const glm::vec2 far{13.0f, 13.0f}; // out of pickup range
    world.spawn_pickup(world.content.find_item("fury"), far);
    world.spawn_pickup(world.content.find_item("heart"), far + glm::vec2{1.0f, 0.0f});

    ContentDB fresh = world.content;
    REQUIRE(fresh.load_items_from_string(R"({"heart": { "sprite": "heart" }})"));
    world.apply_content(std::move(fresh));

    int pickups = 0;
    for (auto [e, pk] : world.reg.view<Pickup>().each()) {
        ++pickups;
        CHECK(world.content.items[pk.item].id == "heart");
    }
    CHECK(pickups == 1); // fury's pickup was destroyed
}

TEST_CASE("generator sprinkles reachable item spots that worlds spawn from") {
    GenParams params;
    params.width = 48;
    params.height = 48;
    params.seed = 1234;
    const DungeonResult a = generate_dungeon(params);
    const DungeonResult b = generate_dungeon(params);
    CHECK(a.item_spawns == b.item_spawns); // deterministic
    for (const glm::ivec2 p : a.item_spawns) {
        CHECK(a.map.tiles.at(p.x, p.y) == Tile::Floor);
        CHECK(p != a.exit_pos);
        CHECK(p != a.player_spawn);
    }

    World world = make_world();
    // make_world's hand map has no item_spawns; feed one through the real path.
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    d.rooms.push_back({1, 1, 6, 6});
    d.player_spawn = {1, 1};
    d.item_spawns.push_back({5, 5});
    world.init_from_dungeon(std::move(d), 3);
    CHECK(world.reg.view<Pickup>().size() == 1);
}
