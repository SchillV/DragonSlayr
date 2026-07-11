#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

using namespace ds;
using doctest::Approx;

namespace {

// Minimal world (no content, so no enemies) for the Health-sync integration.
World tiny_world() {
    World world;
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    for (int i = 0; i < 8; ++i) {
        d.map.tiles.at(i, 0) = Tile::Wall;
        d.map.tiles.at(i, 7) = Tile::Wall;
        d.map.tiles.at(0, i) = Tile::Wall;
        d.map.tiles.at(7, i) = Tile::Wall;
    }
    d.rooms.push_back({1, 1, 6, 6});
    d.player_spawn = {3, 3};
    world.init_from_dungeon(std::move(d), 1);
    return world;
}

} // namespace

TEST_CASE("defaults: cached mirrors base") {
    const StatBlock sb;
    CHECK(sb.cached.max_hp == Approx(100.0f));
    CHECK(sb.cached.move_speed_mult == Approx(1.0f));
    CHECK(sb.cached.damage_mult == Approx(1.0f));
    CHECK(sb.cached.defense_pct == Approx(0.0f));
    CHECK(sb.cached.fire_rate_mult == Approx(1.0f));
}

TEST_CASE("adds apply before mults regardless of insertion order") {
    StatBlock a;
    a.add({StatId::MaxHp, Modifier::Op::Add, 20.0f, 1});
    a.add({StatId::MaxHp, Modifier::Op::Mult, 1.5f, 2});

    StatBlock b;
    b.add({StatId::MaxHp, Modifier::Op::Mult, 1.5f, 2});
    b.add({StatId::MaxHp, Modifier::Op::Add, 20.0f, 1});

    CHECK(a.cached.max_hp == Approx((100.0f + 20.0f) * 1.5f));
    CHECK(b.cached.max_hp == Approx(a.cached.max_hp));
}

TEST_CASE("multiple mults compose multiplicatively") {
    StatBlock sb;
    sb.add({StatId::DamageMult, Modifier::Op::Mult, 1.5f, 1});
    sb.add({StatId::DamageMult, Modifier::Op::Mult, 2.0f, 2});
    CHECK(sb.cached.damage_mult == Approx(3.0f));
}

TEST_CASE("safety clamps: defense cap, hp/rate floors") {
    StatBlock sb;
    sb.add({StatId::DefensePct, Modifier::Op::Add, 5.0f, 1});
    CHECK(sb.cached.defense_pct == Approx(0.9f)); // never immune

    sb.add({StatId::MaxHp, Modifier::Op::Mult, 0.0f, 2});
    CHECK(sb.cached.max_hp == Approx(1.0f)); // never zero max hp

    sb.add({StatId::FireRateMult, Modifier::Op::Mult, 0.0f, 3});
    CHECK(sb.cached.fire_rate_mult == Approx(0.05f)); // cooldowns stay finite
}

TEST_CASE("remove_source strips exactly that source's modifiers") {
    StatBlock sb;
    sb.add({StatId::MaxHp, Modifier::Op::Add, 25.0f, /*source=*/7});
    sb.add({StatId::DamageMult, Modifier::Op::Mult, 2.0f, /*source=*/7});
    sb.add({StatId::MaxHp, Modifier::Op::Add, 10.0f, /*source=*/8});

    sb.remove_source(7);
    CHECK(sb.cached.max_hp == Approx(110.0f));
    CHECK(sb.cached.damage_mult == Approx(1.0f));

    sb.remove_source(8);
    CHECK(sb.cached.max_hp == Approx(100.0f));
    CHECK(sb.mods.empty());
}

TEST_CASE("stat names round-trip") {
    for (size_t i = 0; i < static_cast<size_t>(StatId::Count); ++i) {
        const auto id = static_cast<StatId>(i);
        StatId parsed = StatId::Count;
        CHECK(stat_from_name(stat_name(id), parsed));
        CHECK(parsed == id);
    }
    StatId dummy;
    CHECK_FALSE(stat_from_name("swagger", dummy));
}

TEST_CASE("player health seeds from the stat block, not a hardcoded number") {
    World world = tiny_world();
    auto& sb = world.reg.get<StatBlock>(world.player);
    const auto& hp = world.reg.get<Health>(world.player);
    CHECK(hp.max_hp == Approx(sb.cached.max_hp));
    CHECK(hp.hp == Approx(sb.cached.max_hp));
}

TEST_CASE("refresh_player_stats heals by gained max hp and clamps on loss") {
    World world = tiny_world();
    auto& sb = world.reg.get<StatBlock>(world.player);
    auto& hp = world.reg.get<Health>(world.player);

    hp.hp = 60.0f; // took some damage
    sb.mods.push_back({StatId::MaxHp, Modifier::Op::Add, 50.0f, 3});
    world.refresh_player_stats();
    CHECK(hp.max_hp == Approx(150.0f));
    CHECK(hp.hp == Approx(110.0f)); // healed by the +50 gain

    sb.remove_source(3);          // remove_source recomputes the block...
    world.refresh_player_stats(); // ...and this syncs Health back down
    CHECK(hp.max_hp == Approx(100.0f));
    CHECK(hp.hp == Approx(100.0f)); // clamped to the new max
}
