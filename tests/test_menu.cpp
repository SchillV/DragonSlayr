#include <doctest/doctest.h>

#include "core/cvar.hpp"
#include "game/menu.hpp"
#include "render/font.hpp"

using namespace ds;

namespace {

// Settings sliders bind live cvars; register the ones the menu references so
// the Settings screen is fully populated in the test binary.
void ensure_cvars() {
    if (!cvar_find("r.fov")) cvar_register("r.fov", 75.0f, "");
    if (!cvar_find("in.sensitivity")) cvar_register("in.sensitivity", 2.2f, "");
    if (!cvar_find("snd.volume")) cvar_register("snd.volume", 0.8f, "");
    if (!cvar_find("fx.shake")) cvar_register("fx.shake", 1.0f, "");
}

MenuInput nav(bool up = false, bool down = false, bool sel = false, bool back = false) {
    MenuInput in;
    in.up = up;
    in.down = down;
    in.select = sel;
    in.back = back;
    return in;
}

constexpr glm::vec2 kVp{1280.0f, 720.0f};

size_t index_of(const MenuSystem& m, std::string_view label) {
    for (size_t i = 0; i < m.items().size(); ++i) {
        if (m.items()[i].label == label) return i;
    }
    return SIZE_MAX;
}

} // namespace

TEST_CASE("title screen starts on the first enabled item and can start a run") {
    MenuSystem m;
    m.open(MenuScreen::Title);
    REQUIRE(m.active());
    CHECK(m.items()[m.selection()].label == "NEW GAME");
    CHECK(m.update(nav(false, false, /*sel=*/true), kVp) == MenuAction::StartRun);
}

TEST_CASE("navigation skips disabled placeholders and wraps around") {
    MenuSystem m;
    m.open(MenuScreen::Title);
    // NEW GAME -> (skip CONTINUE/CLASS/LEADERBOARD placeholders) -> OPTIONS.
    m.update(nav(false, true), kVp);
    CHECK(m.items()[m.selection()].label == "OPTIONS");
    // Down again -> ABANDON (last), down again wraps to NEW GAME.
    m.update(nav(false, true), kVp);
    CHECK(m.items()[m.selection()].label == "ABANDON");
    m.update(nav(false, true), kVp);
    CHECK(m.items()[m.selection()].label == "NEW GAME");
    // Up from the top wraps to the last enabled item.
    m.update(nav(true), kVp);
    CHECK(m.items()[m.selection()].label == "ABANDON");
}

TEST_CASE("submenu push and back pop") {
    MenuSystem m;
    m.open(MenuScreen::Pause);
    CHECK(m.depth() == 1);
    // Select OPTIONS -> pushes Settings.
    m.selection(); // no-op read
    while (m.items()[m.selection()].label != "OPTIONS") {
        m.update(nav(false, true), kVp);
    }
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::None);
    CHECK(m.current() == MenuScreen::Settings);
    CHECK(m.depth() == 2);
    // Back pops to Pause.
    m.update(nav(false, false, false, /*back=*/true), kVp);
    CHECK(m.current() == MenuScreen::Pause);
    CHECK(m.depth() == 1);
}

TEST_CASE("pause resume and escape both resume") {
    MenuSystem m;
    m.open(MenuScreen::Pause);
    CHECK(m.items()[m.selection()].label == "RESUME");
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::Resume);

    m.open(MenuScreen::Pause);
    CHECK(m.update(nav(false, false, false, true), kVp) == MenuAction::Resume); // Esc on root
}

TEST_CASE("settings sliders adjust and clamp their cvars") {
    ensure_cvars();
    CVar* shake = cvar_find("fx.shake");
    REQUIRE(shake != nullptr);
    cvar_set(*shake, 1.0f);

    MenuSystem m;
    m.open(MenuScreen::Settings);
    const size_t idx = index_of(m, "SCREEN SHAKE");
    REQUIRE(idx != SIZE_MAX);
    // Navigate to it.
    while (m.selection() != idx) {
        m.update(nav(false, true), kVp);
    }
    MenuInput right;
    right.right = true;
    m.update(right, kVp);
    CHECK(shake->value == doctest::Approx(1.1f));

    // Selecting a slider is not an action.
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::None);

    // Clamps at the max (2.0).
    for (int i = 0; i < 50; ++i) {
        m.update(right, kVp);
    }
    CHECK(shake->value == doctest::Approx(2.0f));
}

TEST_CASE("mouse hover selects and click activates") {
    MenuSystem m;
    m.open(MenuScreen::Death);
    const size_t retry = index_of(m, "RETURN TO TITLE");
    REQUIRE(retry != SIZE_MAX);
    const glm::vec4 r = m.item_rect(retry);

    MenuInput hover;
    hover.mouse_moved = true;
    hover.mouse_px = {r.x + r.z * 0.5f, r.y + r.w * 0.5f};
    m.update(hover, kVp);
    CHECK(m.selection() == retry);

    MenuInput click = hover;
    click.click = true;
    CHECK(m.update(click, kVp) == MenuAction::QuitToTitle);
}

TEST_CASE("death screen shows a status line and delve-again restarts") {
    MenuSystem m;
    m.open(MenuScreen::Death);
    m.set_status_line("FINAL SCORE  1234");
    CHECK(m.items()[m.selection()].label == "DELVE AGAIN");
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::Restart);
}

TEST_CASE("render emits geometry for the title screen") {
    const FontAtlas font = bake_builtin_font();
    MenuSystem m;
    m.open(MenuScreen::Title);
    m.update(MenuInput{}, kVp); // sets viewport

    FrameView view;
    m.render(view, font);
    CHECK(view.overlay.size() > 0);      // panel/dim/bullets
    CHECK(view.overlay_text.size() > 0); // labels
}

TEST_CASE("closed menu is inert") {
    MenuSystem m;
    CHECK_FALSE(m.active());
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::None);
    FrameView view;
    m.render(view, bake_builtin_font());
    CHECK(view.overlay.empty());
    CHECK(view.overlay_text.empty());
}
