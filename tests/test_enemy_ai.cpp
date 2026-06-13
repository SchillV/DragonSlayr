#include <doctest/doctest.h>

#include "core/cvar.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/world.hpp"

using namespace ds;

namespace {

// Short windups keep the integration tests fast.
constexpr const char* kEnemies = R"({
  "walker": {"hp":12,"speed":3.0,"sprite":"m","aggro_radius":14,"behavior":"chaser",
             "attack":{"damage":6,"range":1.0,"windup_s":0.2,"cooldown_s":1.0}},
  "caster": {"hp":10,"speed":2.5,"sprite":"m","aggro_radius":16,"behavior":"ranged","keep_distance":6,
             "attack":{"damage":8,"range":9,"windup_s":0.25,"cooldown_s":1.0,
                       "projectile_speed":10,"projectile_radius":0.2}},
  "hound":  {"hp":16,"speed":4.0,"sprite":"m","aggro_radius":16,"behavior":"charger","lunge_speed":20,
             "attack":{"damage":12,"range":5,"windup_s":0.2,"cooldown_s":1.0}}
})";

// Open walled arena with the player at (4.5, 4.5) and no auto-spawned enemies.
World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(24, 24, Tile::Floor);
    for (int i = 0; i < 24; ++i) {
        d.map.tiles.at(i, 0) = Tile::Wall;
        d.map.tiles.at(i, 23) = Tile::Wall;
        d.map.tiles.at(0, i) = Tile::Wall;
        d.map.tiles.at(23, i) = Tile::Wall;
    }
    d.rooms.push_back({1, 1, 22, 22});
    d.player_spawn = {4, 4};
    d.exit_pos = {20, 20};
    world.init_from_dungeon(std::move(d), 7);
    std::string fb;
    con_execute("sv.god 0", fb); // global cvar — other tests may have toggled it
    return world;
}

float tick_until_hurt(World& world, int max_ticks) {
    const float hp0 = world.reg.get<Health>(world.player).hp;
    for (int i = 0; i < max_ticks; ++i) {
        world.tick(PlayerCmd{}, 1.0f / 60.0f); // player stands still
        if (world.reg.get<Health>(world.player).hp < hp0) {
            break;
        }
    }
    return world.reg.get<Health>(world.player).hp;
}

} // namespace

TEST_CASE("behavior parses and defaults to chaser") {
    ContentDB db;
    std::string err;
    REQUIRE(db.load_enemies_from_string(
        R"({"a":{"hp":1,"sprite":"m"},"b":{"hp":1,"sprite":"m","behavior":"ranged"},
            "c":{"hp":1,"sprite":"m","behavior":"charger"},"d":{"hp":1,"sprite":"m","behavior":"stationary"}})",
        &err));
    CHECK(db.enemies[static_cast<size_t>(db.find_enemy("a"))].behavior == EnemyBehavior::Chaser);
    CHECK(db.enemies[static_cast<size_t>(db.find_enemy("b"))].behavior == EnemyBehavior::Ranged);
    CHECK(db.enemies[static_cast<size_t>(db.find_enemy("c"))].behavior == EnemyBehavior::Charger);
    CHECK(db.enemies[static_cast<size_t>(db.find_enemy("d"))].behavior == EnemyBehavior::Stationary);

    ContentDB bad;
    err.clear();
    CHECK_FALSE(bad.load_enemies_from_string(
        R"({"x":{"hp":1,"sprite":"m","behavior":"flying"}})", &err));
    CHECK(err.find("behavior") != std::string::npos);
}

TEST_CASE("ranged enemy fires a projectile that damages the player") {
    World world = make_world();
    world.spawn_enemy(world.content.find_enemy("caster"), {4.5f, 10.5f}); // 6 tiles N, clear LOS

    const float hp = tick_until_hurt(world, 300);
    CHECK(hp < 100.0f);
    CHECK(hp == doctest::Approx(92.0f)); // one 8-damage bolt
}

TEST_CASE("charger lunges into the player") {
    World world = make_world();
    world.spawn_enemy(world.content.find_enemy("hound"), {4.5f, 9.5f}); // 5 tiles away

    const float hp = tick_until_hurt(world, 300);
    CHECK(hp <= 88.0f); // a 12-damage lunge connected
}

TEST_CASE("chaser still closes in and melees (regression)") {
    World world = make_world();
    world.spawn_enemy(world.content.find_enemy("walker"), {4.5f, 7.0f});

    const float hp = tick_until_hurt(world, 300);
    CHECK(hp < 100.0f);
}

TEST_CASE("enemies do not aggro through walls") {
    World world = make_world();
    // Drop a wall column between player (x=4.5) and a caster to the east.
    for (int y = 1; y < 23; ++y) {
        world.dungeon.map.tiles.at(8, y) = Tile::Wall;
    }
    world.spawn_enemy(world.content.find_enemy("caster"), {12.5f, 4.5f}); // behind the wall

    const float hp = tick_until_hurt(world, 180);
    CHECK(hp == doctest::Approx(100.0f)); // no line of sight → never fires
}
