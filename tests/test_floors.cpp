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
  "fury": { "sprite": "f", "modifiers": [ { "stat": "damage_mult", "op": "mult", "value": 2.0 } ] }
})";

DungeonResult flat_map(glm::ivec2 spawn, glm::ivec2 exit) {
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(16, 16, Tile::Floor);
    for (int i = 0; i < 16; ++i) {
        d.map.tiles.at(i, 0) = Tile::Wall;
        d.map.tiles.at(i, 15) = Tile::Wall;
        d.map.tiles.at(0, i) = Tile::Wall;
        d.map.tiles.at(15, i) = Tile::Wall;
    }
    d.rooms.push_back({1, 1, 14, 14});
    d.player_spawn = spawn;
    d.exit_pos = exit;
    return d;
}

World make_world() {
    World world;
    REQUIRE(world.content.load_enemies_from_string(kEnemies));
    REQUIRE(world.content.load_weapons_from_string(kWeapons));
    REQUIRE(world.content.load_items_from_string(kItems));
    world.init_from_dungeon(flat_map({3, 3}, {12, 12}), 21);
    return world;
}

int count_events(const World& world, EvType type) {
    int n = 0;
    for (const TelemetryEvent& ev : world.telem.events()) {
        n += ev.type == type ? 1 : 0;
    }
    return n;
}

const Room* room_of_type(const DungeonResult& d, RoomType t) {
    for (const Room& r : d.rooms) {
        if (r.type == t) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("stepping onto the stairs raises the exit flag") {
    World world = make_world();
    CHECK_FALSE(world.floor_exit_requested);

    // Teleport next to the stairs and walk onto them.
    world.reg.get<Transform>(world.player).pos = {12.5f, 12.5f};
    world.tick({}, 1.0f / 60.0f);
    CHECK(world.floor_exit_requested);
}

TEST_CASE("advance_floor carries the run and rebuilds the floor") {
    World world = make_world();
    grant_item(world, world.content.find_item("fury"));
    world.score = 730;
    world.reg.get<Health>(world.player).hp = 47.0f;

    // A lingering temp buff that must NOT survive the stairs.
    auto& stats = world.reg.get<StatBlock>(world.player);
    stats.add({StatId::MoveSpeedMult, Modifier::Op::Mult, 1.5f, kTempSourceBase});

    DungeonResult next = flat_map({2, 2}, {13, 13});
    next.enemy_spawns.push_back({10, 10});
    world.advance_floor(std::move(next));

    CHECK(world.current_floor == 2);
    CHECK(world.score == 730);
    CHECK_FALSE(world.floor_exit_requested);

    // Player stands at the new spawn with carried health and items.
    const auto& tr = world.reg.get<Transform>(world.player);
    CHECK(tr.pos.x == Approx(2.5f));
    CHECK(tr.pos.y == Approx(2.5f));
    CHECK(world.reg.get<Health>(world.player).hp == Approx(47.0f));
    REQUIRE(world.reg.get<Inventory>(world.player).items.size() == 1);
    const auto& carried = world.reg.get<StatBlock>(world.player);
    CHECK(carried.cached.damage_mult == Approx(2.0f));     // item modifier survived
    CHECK(carried.cached.move_speed_mult == Approx(1.0f)); // temp buff did not

    // New floor spawned its enemies; telemetry continues the same run.
    CHECK(world.reg.view<Enemy>().size() == 1);
    CHECK(count_events(world, EvType::RunStart) == 1);
    CHECK(count_events(world, EvType::FloorAdvance) == 1);
}

TEST_CASE("dying with 1 hp is impossible when descending") {
    World world = make_world();
    world.reg.get<Health>(world.player).hp = 0.5f; // survivable sliver
    world.advance_floor(flat_map({2, 2}, {13, 13}));
    CHECK(world.reg.get<Health>(world.player).hp >= 1.0f);
    CHECK_FALSE(world.player_dead);
}

TEST_CASE("generator tags an arena and a safe treasure vault") {
    GenParams p;
    p.seed = 5;
    const DungeonResult d = generate_dungeon(p);
    REQUIRE(d.rooms.size() > 2);

    const Room* arena = room_of_type(d, RoomType::Arena);
    const Room* treasure = room_of_type(d, RoomType::Treasure);
    REQUIRE(arena != nullptr);
    REQUIRE(treasure != nullptr);
    CHECK(arena->area() >= treasure->area());
    CHECK(d.rooms[0].type == RoomType::Normal); // spawn room stays neutral

    for (const glm::ivec2 sp : d.enemy_spawns) {
        CHECK_FALSE(treasure->contains(sp)); // vaults are safe
    }
    bool vault_has_item = false;
    for (const glm::ivec2 ip : d.item_spawns) {
        vault_has_item |= treasure->contains(ip);
    }
    CHECK(vault_has_item); // and they pay out
}

TEST_CASE("deeper floors pack more enemies") {
    GenParams shallow;
    shallow.seed = 9;
    GenParams deep = shallow;
    deep.floor = 5;

    const size_t at_1 = generate_dungeon(shallow).enemy_spawns.size();
    const size_t at_5 = generate_dungeon(deep).enemy_spawns.size();
    CHECK(at_5 > at_1);

    // Same tiles either way — scaling only touches spawn counts.
    CHECK(generate_dungeon(shallow).map.hash() == generate_dungeon(deep).map.hash());
}
