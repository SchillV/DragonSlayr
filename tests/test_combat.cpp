#include <doctest/doctest.h>

#include "core/cvar.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

using namespace ds;

namespace {

constexpr const char* kEnemies = R"({"walker": {"hp": 12, "speed": 3.0, "sprite": "monster",
    "score": 50, "attack": {"damage": 6, "range": 0.9}}})";
constexpr const char* kWeapons = R"({
  "sword": {"type": "melee", "damage": 7, "range": 1.6, "arc_deg": 90, "cooldown_s": 0.45},
  "bolt": {"type": "projectile", "damage": 5, "speed": 14.0, "radius": 0.1, "ttl_s": 3.0}})";

// A world on a tiny hand-made open map with no auto-spawned enemies.
World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));

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
    // no enemy_spawns: combat tests place their own
    world.init_from_dungeon(std::move(d), 7);
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

int count_events(const World& world, EvType type) {
    int n = 0;
    for (const TelemetryEvent& ev : world.telem.events()) {
        n += ev.type == type ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("melee arc hits by angle and range") {
    const glm::vec2 origin{0.0f, 0.0f};
    const float yaw = 0.0f; // facing +X
    // dead ahead, in range
    CHECK(in_melee_arc(origin, yaw, 1.6f, 75.0f, {1.0f, 0.0f}));
    // ahead but out of range
    CHECK_FALSE(in_melee_arc(origin, yaw, 1.6f, 75.0f, {2.0f, 0.0f}));
    // 30 degrees off, inside a 75 degree arc
    CHECK(in_melee_arc(origin, yaw, 1.6f, 75.0f, {std::cos(0.5f), std::sin(0.5f)}));
    // 60 degrees off, outside
    CHECK_FALSE(in_melee_arc(origin, yaw, 1.6f, 75.0f, {std::cos(1.05f), std::sin(1.05f)}));
    // directly behind
    CHECK_FALSE(in_melee_arc(origin, yaw, 1.6f, 75.0f, {-1.0f, 0.0f}));
    // wrap-around: facing -X (pi), target at angle just past pi
    CHECK(in_melee_arc(origin, glm::pi<float>(), 1.6f, 75.0f,
                       {-std::cos(0.2f), -std::sin(0.2f)}));
}

TEST_CASE("sword swing damages enemies in the arc, not behind walls or backs") {
    World world = make_world();
    const auto& player_tr = world.reg.get<Transform>(world.player);

    const entt::entity front = spawn_walker(world, player_tr.pos + glm::vec2{1.0f, 0.0f});
    const entt::entity behind = spawn_walker(world, player_tr.pos - glm::vec2{1.0f, 0.0f});

    PlayerCmd cmd;
    cmd.yaw = 0.0f; // facing +X
    cmd.attack_primary = true;
    world.tick(cmd, 1.0f / 60.0f);

    CHECK(world.reg.get<Health>(front).hp == 5.0f); // 12 - 7
    CHECK(world.reg.get<Health>(behind).hp == 12.0f);
    CHECK(count_events(world, EvType::PlayerAttack) == 1);
}

TEST_CASE("killing an enemy scores, records telemetry and destroys it") {
    World world = make_world();
    const auto& player_tr = world.reg.get<Transform>(world.player);
    const entt::entity victim = spawn_walker(world, player_tr.pos + glm::vec2{1.0f, 0.0f});

    PlayerCmd cmd;
    cmd.yaw = 0.0f;
    cmd.attack_primary = true;
    world.tick(cmd, 1.0f / 60.0f); // 12 -> 5
    cmd.attack_primary = false;
    for (int i = 0; i < 30; ++i) {
        world.tick(cmd, 1.0f / 60.0f); // cooldown
    }
    cmd.attack_primary = true;
    world.tick(cmd, 1.0f / 60.0f); // 5 -> dead

    CHECK_FALSE(world.reg.valid(victim));
    CHECK(world.score == 50);
    CHECK(count_events(world, EvType::EnemyKilled) == 1);
}

TEST_CASE("bolts fly, hit the first enemy and expire on walls") {
    World world = make_world();

    // Fire with nothing ahead: bolt should eventually hit the east wall.
    PlayerCmd cmd;
    cmd.yaw = 0.0f;
    cmd.attack_secondary = true;
    world.tick(cmd, 1.0f / 60.0f);
    CHECK(count_events(world, EvType::ProjectileFired) == 1);

    cmd.attack_secondary = false;
    for (int i = 0; i < 120; ++i) {
        world.tick(cmd, 1.0f / 60.0f);
    }
    int projectiles = 0;
    for ([[maybe_unused]] auto e : world.reg.view<Projectile>()) {
        ++projectiles;
    }
    CHECK(projectiles == 0); // gone into the wall
    CHECK(count_events(world, EvType::ProjectileHit) == 1);

    // Now with a target in the way.
    const auto& player_tr = world.reg.get<Transform>(world.player);
    const entt::entity target = spawn_walker(world, player_tr.pos + glm::vec2{3.0f, 0.0f});
    cmd.attack_secondary = true;
    world.tick(cmd, 1.0f / 60.0f);
    cmd.attack_secondary = false;
    for (int i = 0; i < 60 && world.reg.valid(target); ++i) {
        world.tick(cmd, 1.0f / 60.0f);
    }
    CHECK(world.reg.get<Health>(target).hp == 7.0f); // 12 - 5
}

TEST_CASE("player death freezes the run and records run_end") {
    World world = make_world();
    damage_player(world, 250.0f, 0);
    CHECK(world.player_dead);
    CHECK(world.reg.get<Health>(world.player).hp == 0.0f);
    CHECK(count_events(world, EvType::RunEnd) == 1);

    // Dead worlds don't simulate: movement cmds change nothing.
    const glm::vec2 before = world.reg.get<Transform>(world.player).pos;
    PlayerCmd cmd;
    cmd.wish_dir = {1.0f, 0.0f};
    for (int i = 0; i < 30; ++i) {
        world.tick(cmd, 1.0f / 60.0f);
    }
    CHECK(world.reg.get<Transform>(world.player).pos == before);
}

TEST_CASE("god mode blocks damage and counts as a cheat") {
    World world = make_world();
    std::string fb;
    con_execute("sv.god 1", fb);
    damage_player(world, 50.0f, 0);
    CHECK_FALSE(world.player_dead);
    CHECK(world.reg.get<Health>(world.player).hp == 100.0f);
    CHECK(cvar_any_cheat_touched());
    con_execute("sv.god 0", fb);
}
