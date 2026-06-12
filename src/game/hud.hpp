#pragma once

#include "render/frame_view.hpp"

#include <glm/glm.hpp>

namespace ds {

// Overlay texture array layout the HUD assumes (built by the app):
inline constexpr float kOverlayWhite = 0.0f; // 1x1 white, for solid quads
inline constexpr float kOverlaySword = 1.0f;
inline constexpr float kOverlayHand = 2.0f;

struct HudState {
    float hp = 100.0f;
    float max_hp = 100.0f;
    float swing_anim = 0.0f; // 1 -> 0, sword swing
    float cast_anim = 0.0f;  // 1 -> 0, bolt cast
    float hurt_flash = 0.0f; // 1 -> 0, red screen flash
    float dash_cooldown01 = 0.0f; // 0 = ready, 1 = just used
    bool dead = false;
    int score = 0;
};

// Appends crosshair, health bar, hurt flash, viewmodel and death dim quads to
// the frame's overlay list. Pixel-space; viewport in pixels.
void build_hud(FrameView& view, const HudState& state, glm::vec2 viewport);

} // namespace ds
