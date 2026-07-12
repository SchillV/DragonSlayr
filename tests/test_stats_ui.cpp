#include <doctest/doctest.h>

#include "game/stats_ui.hpp"
#include "render/font.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/world.hpp"

using namespace ds;

namespace {

World tiny_world() {
    World world;
    DungeonResult d;
    d.map.tiles = Grid2D<Tile>(8, 8, Tile::Floor);
    d.rooms.push_back({1, 1, 6, 6});
    d.player_spawn = {3, 3};
    world.init_from_dungeon(std::move(d), 71);
    return world;
}

} // namespace

TEST_CASE("describe_attr mirrors the derivation table") {
    CHECK(describe_attr(StatId::Str, 4.0f) == std::vector<std::string>{"+20% MELEE DAMAGE"});
    CHECK(describe_attr(StatId::Mag, 2.0f) == std::vector<std::string>{"+10% MAGIC DAMAGE"});
    CHECK(describe_attr(StatId::Dex, 5.0f) ==
          std::vector<std::string>{"+15% ATTACK SPEED", "+10% MOVE SPEED"});
    CHECK(describe_attr(StatId::Vit, 3.0f) ==
          std::vector<std::string>{"+24 MAX HEALTH", "+3% DEFENSE"});
    // Negatives read as penalties; non-attributes have nothing to say.
    CHECK(describe_attr(StatId::Str, -2.0f) == std::vector<std::string>{"-10% MELEE DAMAGE"});
    CHECK(describe_attr(StatId::MaxHp, 5.0f).empty());
}

TEST_CASE("the character sheet renders and closes on esc") {
    World world = tiny_world();
    const FontAtlas font = bake_builtin_font();

    StatsUi ui;
    CHECK_FALSE(ui.active());
    ui.open();
    REQUIRE(ui.active());

    FrameView view;
    ui.render(view, font, world, {1280.0f, 720.0f});
    CHECK(view.overlay.size() >= 1);      // the dim
    CHECK(view.overlay_text.size() > 10); // header + sheet + rows

    MenuInput esc;
    esc.back = true;
    ui.update(esc);
    CHECK(ui.close_requested());
}
