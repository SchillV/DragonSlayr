#pragma once

#include "render/frame_view.hpp"

#include <glm/glm.hpp>

#include <span>

namespace ds {

struct FontAtlas;

// Overlay texture array layout the HUD assumes (built by the app):
inline constexpr float kOverlayWhite = 0.0f; // 1x1 white, for solid quads
inline constexpr float kOverlaySword = 1.0f;
inline constexpr float kOverlayHand = 2.0f;

// A recent hit on the player, for the screen-edge damage wedges.
struct DamageIndicator {
    float world_angle = 0.0f; // direction the hit came from (world yaw)
    float t = 0.0f;           // 1 -> 0 over its lifetime
};

// An active feat shown in the HUD's bottom bar ("BLOODLUST x3").
struct FeatChip {
    const char* name = "";
    int count = 1;
};

struct HudState {
    float hp = 100.0f;
    float max_hp = 100.0f;
    float swing_anim = 0.0f; // 1 -> 0, sword swing
    float cast_anim = 0.0f;  // 1 -> 0, bolt cast
    float hurt_flash = 0.0f; // 1 -> 0, red screen flash
    float dash_cooldown01 = 0.0f; // 0 = ready, 1 = just used
    bool dead = false;
    int score = 0;
    int floor = 1;
    // combat feedback
    float cam_yaw = 0.0f;
    double time = 0.0;            // seconds, drives low-health pulsing
    float chip_hp = 0.0f;         // trailing "damage you just took" bar value
    float hitmarker_t = 0.0f;     // 1 -> 0 flash when your hit connects
    bool hitmarker_kill = false;  // kill-confirm variant (bigger, red)
    float lowhp_threshold = 0.3f; // fraction of max hp where the warning starts
    std::span<const DamageIndicator> indicators;
    std::span<const FeatChip> feats;
};

// Screen rotation of a damage wedge: 0 = the hit came from straight ahead
// (wedge at the top of the screen), positive = clockwise. Pure, unit-tested.
float indicator_screen_rot(float attack_world_angle, float cam_yaw);

// Appends crosshair, health bar, hurt flash, viewmodel and death dim quads to
// the frame's overlay list (text goes to overlay_text via the font). Pixel
// space; viewport in pixels. `font` may be null (text is skipped).
void build_hud(FrameView& view, const HudState& state, glm::vec2 viewport, const FontAtlas* font);

} // namespace ds
