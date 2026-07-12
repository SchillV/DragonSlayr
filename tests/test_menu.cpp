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

TEST_CASE("title: quick resume gates on a suspended run, new game opens slots") {
    MenuSystem m;
    m.open(MenuScreen::Title);
    REQUIRE(m.active());
    // No suspended run and no saves: first enabled item is NEW GAME.
    CHECK(m.items()[m.selection()].label == "NEW GAME");
    CHECK_FALSE(m.items()[0].enabled); // QUICK RESUME
    CHECK(m.update(nav(false, false, /*sel=*/true), kVp) == MenuAction::None);
    CHECK(m.current() == MenuScreen::SlotNew);

    MenuSystem with_run;
    with_run.set_resume_available(true);
    with_run.open(MenuScreen::Title);
    CHECK(with_run.items()[with_run.selection()].label == "QUICK RESUME");
    CHECK(with_run.update(nav(false, false, true), kVp) == MenuAction::Resume);
}

TEST_CASE("navigation skips disabled items and wraps around") {
    MenuSystem m;
    m.open(MenuScreen::Title); // QUICK RESUME + LOAD GAME disabled (no saves)
    CHECK(m.items()[m.selection()].label == "NEW GAME");
    m.update(nav(false, true), kVp);
    CHECK(m.items()[m.selection()].label == "OPTIONS");
    m.update(nav(false, true), kVp);
    CHECK(m.items()[m.selection()].label == "QUIT TO DESKTOP");
    m.update(nav(false, true), kVp); // wraps
    CHECK(m.items()[m.selection()].label == "NEW GAME");
    m.update(nav(true), kVp); // up from the top wraps to the last enabled
    CHECK(m.items()[m.selection()].label == "QUIT TO DESKTOP");
}

TEST_CASE("save slots: load gates on occupancy, new arms before overwriting") {
    MenuSystem m;
    m.set_roster(MenuScreen::SlotNew, {{"SLOT 1 · KNIGHT", "", 1, true},
                                       {"SLOT 2 · EMPTY", "", 2, false}});
    m.set_roster(MenuScreen::SlotLoad, {{"SLOT 1 · KNIGHT", "", 1, true},
                                        {"SLOT 2 · EMPTY", "", 2, false}});

    // LOAD GAME enables once any slot is occupied; empty rows stay disabled.
    m.open(MenuScreen::Title);
    while (m.items()[m.selection()].label != "LOAD GAME") {
        m.update(nav(false, true), kVp);
    }
    m.update(nav(false, false, true), kVp);
    REQUIRE(m.current() == MenuScreen::SlotLoad);
    CHECK(m.items()[0].enabled);
    CHECK_FALSE(m.items()[1].enabled);
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::LoadSlot);
    CHECK(m.chosen_payload() == 1);

    // NEW GAME on an empty slot fires immediately; an occupied slot arms
    // first and confirms on the second press.
    m.open(MenuScreen::SlotNew);
    m.update(nav(false, true), kVp); // onto SLOT 2 (empty)
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::NewGameSlot);
    CHECK(m.chosen_payload() == 2);

    m.open(MenuScreen::SlotNew); // back on SLOT 1 (occupied)
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::None);
    CHECK(m.items()[0].label == "OVERWRITE THIS FATE? (CONFIRM)");
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::NewGameSlot);
    CHECK(m.chosen_payload() == 1);

    // Arming resets when the selection moves away.
    m.open(MenuScreen::SlotNew);
    m.update(nav(false, false, true), kVp); // arm slot 1
    m.update(nav(false, true), kVp);        // move off
    CHECK(m.items()[0].label == "SLOT 1 · KNIGHT");
}

TEST_CASE("hub lists the camp stations and resume descent when suspended") {
    MenuSystem m;
    m.open(MenuScreen::Hub);
    CHECK(m.items()[m.selection()].label == "DESCEND");

    m.set_resume_available(true);
    m.open(MenuScreen::Hub);
    CHECK(m.items()[m.selection()].label == "RESUME DESCENT");
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::Resume);

    // TRAIN reaches class select and pops back.
    m.set_class_roster({{"KNIGHT", "", 0}}, 0);
    while (m.items()[m.selection()].label != "TRAIN") {
        m.update(nav(false, true), kVp);
    }
    m.update(nav(false, false, true), kVp);
    CHECK(m.current() == MenuScreen::ClassSelect);
    m.update(nav(false, false, false, true), kVp);
    CHECK(m.current() == MenuScreen::Hub);
}

TEST_CASE("sanctum reports the chosen upgrade") {
    MenuSystem m;
    m.set_roster(MenuScreen::Sanctum,
                 {{"TOUGH HIDE · RANK 0/5 · COST 100", "+10 hp", 0, true},
                  {"KEEN EDGE · RANK 5/5 · MAX", "+5% melee", 1, false}});
    m.open(MenuScreen::Sanctum);
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::BuyUpgrade);
    CHECK(m.chosen_payload() == 0);
}

TEST_CASE("class select lists the injected roster and reports the choice") {
    MenuSystem m;
    m.set_class_roster({{"KNIGHT", "Steel.", 0}, {"MAGE", "Sparks.", 2}}, /*current=*/0);
    m.open(MenuScreen::ClassSelect);

    REQUIRE(m.items().size() == 3); // two classes + BACK
    CHECK(m.items()[0].label == "KNIGHT  · CHOSEN");
    CHECK(m.items()[1].label == "MAGE");
    CHECK(m.items()[0].blurb == "Steel.");

    // Pick MAGE: action fires with its payload and the marker moves.
    m.update(nav(false, true), kVp);
    CHECK(m.update(nav(false, false, true), kVp) == MenuAction::SelectClass);
    CHECK(m.chosen_payload() == 2);
    CHECK(m.items()[1].label == "MAGE  · CHOSEN");

    // BACK pops (Title -> ClassSelect stack not present here; opened as root,
    // so back keeps the screen but BACK still pops when stacked).
    m.set_class_roster({}, 0);
    m.open(MenuScreen::ClassSelect);
    CHECK(m.items()[0].label == "NO CLASSES DEFINED");
    CHECK_FALSE(m.items()[0].enabled);
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
    const size_t retry = index_of(m, "RETURN TO CAMP");
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
