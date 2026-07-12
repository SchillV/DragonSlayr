#include <doctest/doctest.h>

#include "sim/boss.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

using namespace ds;
using doctest::Approx;

namespace {

constexpr const char* kEnemies = R"({"walker": {"hp": 12, "speed": 3.0, "sprite": "m",
    "score": 50, "attack": {"damage": 6, "range": 0.9}}})";
constexpr const char* kWeapons = R"({
  "sword": {"type": "melee", "damage": 7, "range": 1.6, "arc_deg": 90, "cooldown_s": 0.45},
  "bolt": {"type": "projectile", "damage": 5, "speed": 14.0, "radius": 0.1, "ttl_s": 3.0}})";
constexpr const char* kBosses = R"({
  "tyrant": {
    "sprite": "t", "hp": 100, "hp_per_floor": 10, "speed": 2.0, "radius": 0.7,
    "aggro_radius": 10.0, "contact_damage": 18, "phase2_at": 0.5,
    "score": 600, "xp": 130,
    "patterns": [
      { "pattern": "ground_slam", "weight": 1.0, "cooldown_s": 3.0, "damage": 25, "radius": 2.5 },
      { "pattern": "charge", "weight": 1.0, "cooldown_s": 4.0, "damage": 20, "speed": 15.0 },
      { "pattern": "projectile_ring", "weight": 1.0, "cooldown_s": 5.0, "damage": 10, "count": 8 },
      { "pattern": "summon_adds", "weight": 1.0, "cooldown_s": 8.0, "count": 3 }
    ]
  }
})";

DungeonResult flat_map() {
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
    return d;
}

// A world on boss floor 3 with the boss at the room center.
World boss_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_bosses_from_string(kBosses));
    world.init_from_dungeon(flat_map(), 81);
    world.advance_floor(flat_map());
    world.advance_floor(flat_map()); // floor 3: the seal
    return world;
}

Boss& the_boss(World& world) {
    return world.reg.get<Boss>(world.boss_entity);
}

int pattern_index(const World& world, BossPattern p) {
    const BossDef& def = world.content.bosses[0];
    for (size_t i = 0; i < def.patterns.size(); ++i) {
        if (def.patterns[i].pattern == p) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void engage(World& world) {
    // Stand next to the boss and let one tick run the aggro check.
    const glm::vec2 bpos = world.reg.get<Transform>(world.boss_entity).pos;
    world.reg.get<Transform>(world.player).pos = bpos + glm::vec2{3.0f, 0.0f};
    world.tick({}, 1.0f / 60.0f);
}

} // namespace

TEST_CASE("boss defs parse with their pattern lists") {
    ContentDB db;
    REQUIRE(db.load_bosses_from_string(kBosses));
    REQUIRE(db.bosses.size() == 1);
    const BossDef& def = db.bosses[0];
    CHECK(def.hp == Approx(100.0f));
    REQUIRE(def.patterns.size() == 4);
    CHECK(def.patterns[0].pattern == BossPattern::GroundSlam);
    CHECK(def.patterns[0].radius == Approx(2.5f));
}

TEST_CASE("every third floor spawns a boss whose hp scales with depth") {
    World world = boss_world();
    REQUIRE(world.boss_alive());
    CHECK(world.current_floor == 3);
    // hp 100 + 10 per floor beyond the first = 120 on floor 3.
    CHECK(world.reg.get<Health>(world.boss_entity).max_hp == Approx(120.0f));

    // Floors 4 and 5 are boss-free; floor 6 seals again.
    damage_boss(world, world.boss_entity, 9999.0f, 0);
    world.advance_floor(flat_map());
    CHECK_FALSE(world.boss_alive());
    world.advance_floor(flat_map());
    CHECK_FALSE(world.boss_alive());
    world.advance_floor(flat_map());
    CHECK(world.boss_alive());
}

TEST_CASE("a living boss seals the stairs") {
    World world = boss_world();
    world.reg.get<Transform>(world.player).pos = {20.5f, 20.5f}; // on the stairs
    world.tick({}, 1.0f / 60.0f);
    CHECK_FALSE(world.floor_exit_requested);
    CHECK(world.seal_hint > 0.0f);

    damage_boss(world, world.boss_entity, 9999.0f, 0);
    CHECK_FALSE(world.boss_alive());
    world.tick({}, 1.0f / 60.0f);
    CHECK(world.floor_exit_requested);
}

TEST_CASE("killing the boss pays out and records the fight") {
    World world = boss_world();
    engage(world);
    REQUIRE(the_boss(world).engaged);

    const int score_before = world.score;
    damage_boss(world, world.boss_entity, 9999.0f, 0);
    CHECK(world.score == score_before + 600);
    // 130 boss xp crosses the first threshold (60): level 2, 70 remainder.
    CHECK(world.level == 2);
    CHECK(world.xp == Approx(70.0f));
    CHECK(world.skill_points == 1);

    int engaged = 0, killed = 0;
    for (const TelemetryEvent& ev : world.telem.events()) {
        engaged += ev.type == EvType::BossEngaged ? 1 : 0;
        killed += ev.type == EvType::BossKilled ? 1 : 0;
    }
    CHECK(engaged == 1);
    CHECK(killed == 1);
}

TEST_CASE("ground slam telegraphs then strikes the marked ground") {
    World world = boss_world();
    engage(world);
    Boss& boss = the_boss(world);

    boss.active_pattern = static_cast<uint8_t>(pattern_index(world, BossPattern::GroundSlam));
    boss.pattern_time = 0.0f;
    boss.slam_pos = world.reg.get<Transform>(world.player).pos;

    // Mid-windup: no damage yet.
    world.tick({}, 1.0f / 60.0f);
    CHECK(world.reg.get<Health>(world.player).hp == Approx(100.0f));

    // Let the windup elapse while standing in the circle.
    for (int i = 0; i < 60 && boss.active_pattern != 0xff; ++i) {
        world.tick({}, 1.0f / 60.0f);
    }
    CHECK(world.reg.get<Health>(world.player).hp == Approx(75.0f)); // 25 slam
}

TEST_CASE("projectile ring fires a radial volley") {
    World world = boss_world();
    engage(world);
    Boss& boss = the_boss(world);
    boss.active_pattern = static_cast<uint8_t>(pattern_index(world, BossPattern::ProjectileRing));
    boss.pattern_time = 0.0f;

    world.tick({}, 1.0f / 60.0f);
    int enemy_shots = 0;
    for (auto [e, proj] : world.reg.view<Projectile>().each()) {
        enemy_shots += proj.team == Team::Enemy ? 1 : 0;
    }
    CHECK(enemy_shots == 8);
}

TEST_CASE("summon adds respects the alive cap") {
    World world = boss_world();
    engage(world);
    Boss& boss = the_boss(world);
    const auto count_enemies = [&world] {
        int n = 0;
        for ([[maybe_unused]] auto e : world.reg.view<Enemy>()) {
            ++n;
        }
        return n;
    };
    const int before = count_enemies();

    boss.active_pattern = static_cast<uint8_t>(pattern_index(world, BossPattern::SummonAdds));
    boss.pattern_time = 0.0f;
    world.tick({}, 1.0f / 60.0f);
    CHECK(count_enemies() == before + 3);
}

TEST_CASE("phase two arrives at the hp threshold") {
    World world = boss_world();
    engage(world);
    CHECK(the_boss(world).phase == 1);
    damage_boss(world, world.boss_entity, 70.0f, 0); // 120 -> 50 (< 50%)
    world.tick({}, 1.0f / 60.0f);
    CHECK(the_boss(world).phase == 2);
}

TEST_CASE("brain tag weights steer the pattern draw") {
    World world = boss_world();
    engage(world);

    // Zero every tag except Kite -> only the charge (kite-punisher) fires.
    world.boss_tag_weights = {0.0f, 0.0f, 0.0f, 0.0f};
    world.boss_tag_weights[static_cast<size_t>(CounterTag::Kite)] = 1.0f;

    Boss& boss = the_boss(world);
    boss.pattern_cooldown = 0.0f;
    for (int i = 0; i < 240 && boss.active_pattern == 0xff; ++i) {
        world.tick({}, 1.0f / 60.0f);
    }
    REQUIRE(boss.active_pattern != 0xff);
    CHECK(world.content.bosses[0].patterns[boss.active_pattern].pattern == BossPattern::Charge);
}
