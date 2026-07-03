#include <doctest/doctest.h>

#include "game/hud.hpp"

#include <glm/gtc/constants.hpp>

using namespace ds;
using doctest::Approx;

namespace {

constexpr float kPi = glm::pi<float>();

int count_red_solids(const FrameView& view) {
    int n = 0;
    for (const OverlayQuad& q : view.overlay) {
        n += (q.color.r > 0.85f && q.color.g < 0.25f && q.color.b < 0.25f) ? 1 : 0;
    }
    return n;
}

HudState healthy_state() {
    HudState s;
    s.hp = 100.0f;
    s.max_hp = 100.0f;
    s.chip_hp = 100.0f;
    return s;
}

} // namespace

TEST_CASE("indicator screen rotation points at the attacker") {
    // Hit from where the camera faces -> wedge at the top (rot 0).
    CHECK(indicator_screen_rot(1.3f, 1.3f) == Approx(0.0f));
    // Hit from 90 degrees clockwise of the view direction.
    CHECK(indicator_screen_rot(glm::half_pi<float>(), 0.0f) == Approx(glm::half_pi<float>()));
    // Hit from behind -> +/- pi.
    CHECK(std::abs(indicator_screen_rot(kPi, 0.0f)) == Approx(kPi));
    // Wrap-around stays in [-pi, pi].
    const float wrapped = indicator_screen_rot(-3.0f, 3.0f);
    CHECK(wrapped == Approx(2.0f * kPi - 6.0f));
    CHECK(std::abs(wrapped) <= kPi);
}

TEST_CASE("damage indicators emit rotated wedges") {
    HudState s = healthy_state();
    const DamageIndicator inds[] = {{0.7f, 1.0f}, {0.7f + kPi, 0.5f}};
    s.cam_yaw = 0.7f;
    s.indicators = inds;

    FrameView view;
    build_hud(view, s, {1280.0f, 720.0f}, nullptr);

    bool found_front = false;
    bool found_behind = false;
    for (const OverlayQuad& q : view.overlay) {
        if (q.color.r > 0.85f && q.color.g < 0.25f) {
            found_front |= std::abs(q.rot) < 0.01f;
            found_behind |= std::abs(std::abs(q.rot) - kPi) < 0.01f;
        }
    }
    CHECK(found_front);
    CHECK(found_behind);
}

TEST_CASE("hitmarker draws four ticks, kill variant is red") {
    HudState s = healthy_state();
    s.hitmarker_t = 1.0f;
    s.hitmarker_kill = false;

    FrameView white_view;
    build_hud(white_view, s, {1280.0f, 720.0f}, nullptr);
    int white_ticks = 0;
    for (const OverlayQuad& q : white_view.overlay) {
        white_ticks += (q.color.r > 0.9f && q.color.g > 0.9f && q.rot != 0.0f) ? 1 : 0;
    }
    CHECK(white_ticks == 4);

    s.hitmarker_kill = true;
    FrameView red_view;
    build_hud(red_view, s, {1280.0f, 720.0f}, nullptr);
    CHECK(count_red_solids(red_view) >= 4);
}

TEST_CASE("low health draws the pulsing border, full health does not") {
    HudState low = healthy_state();
    low.hp = 15.0f;
    low.chip_hp = 15.0f;
    low.time = 0.1; // some nonzero pulse phase

    FrameView low_view;
    build_hud(low_view, low, {1280.0f, 720.0f}, nullptr);

    FrameView full_view;
    build_hud(full_view, healthy_state(), {1280.0f, 720.0f}, nullptr);

    CHECK(low_view.overlay.size() > full_view.overlay.size());
}

TEST_CASE("health chip appears only while trailing above current hp") {
    HudState s = healthy_state();
    s.hp = 40.0f;
    s.chip_hp = 70.0f;

    FrameView chipped;
    build_hud(chipped, s, {1280.0f, 720.0f}, nullptr);

    s.chip_hp = 40.0f;
    FrameView flush;
    build_hud(flush, s, {1280.0f, 720.0f}, nullptr);

    CHECK(chipped.overlay.size() == flush.overlay.size() + 1);
}
