#include "game/hud.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

void push_solid(FrameView& view, glm::vec2 pos, glm::vec2 size, glm::vec4 color) {
    OverlayQuad q;
    q.pos = pos;
    q.size = size;
    q.layer = kOverlayWhite;
    q.color = color;
    view.overlay.push_back(q);
}

void push_textured(FrameView& view, glm::vec2 pos, glm::vec2 size, float layer, float alpha) {
    OverlayQuad q;
    q.pos = pos;
    q.size = size;
    q.layer = layer;
    q.color = {1.0f, 1.0f, 1.0f, alpha};
    view.overlay.push_back(q);
}

} // namespace

void build_hud(FrameView& view, const HudState& state, glm::vec2 vp) {
    const float scale = std::max(1.0f, std::round(vp.y / 360.0f)); // retro pixel scale

    // Hurt flash (under everything else so the HUD stays readable).
    if (state.hurt_flash > 0.0f) {
        push_solid(view, {0, 0}, vp, {0.8f, 0.05f, 0.05f, state.hurt_flash * 0.35f});
    }

    // Viewmodel: sword bottom-right, lunging up-left mid-swing; casting hand
    // pops bottom-center while a bolt is on cooldown's leading edge.
    {
        const float vm = 96.0f * scale;
        const float lunge = std::sin(state.swing_anim * glm::pi<float>());
        const glm::vec2 sword_pos{vp.x - vm * (1.05f + 0.35f * lunge),
                                  vp.y - vm * (0.95f + 0.45f * lunge)};
        push_textured(view, sword_pos, {vm, vm}, kOverlaySword, 1.0f);

        if (state.cast_anim > 0.0f) {
            const float rise = std::sin(state.cast_anim * glm::pi<float>());
            const glm::vec2 hand_pos{vp.x * 0.5f - vm * 0.5f, vp.y - vm * rise};
            push_textured(view, hand_pos, {vm, vm}, kOverlayHand, 1.0f);
        }
    }

    // Crosshair: four ticks, red while swinging (legacy behavior).
    {
        const glm::vec4 col = state.swing_anim > 0.0f ? glm::vec4{0.9f, 0.2f, 0.2f, 0.9f}
                                                      : glm::vec4{1.0f, 1.0f, 1.0f, 0.8f};
        const glm::vec2 c = vp * 0.5f;
        const float t = 2.0f * scale; // tick thickness
        const float len = 5.0f * scale;
        const float gap = 3.0f * scale;
        push_solid(view, {c.x - gap - len, c.y - t * 0.5f}, {len, t}, col);
        push_solid(view, {c.x + gap, c.y - t * 0.5f}, {len, t}, col);
        push_solid(view, {c.x - t * 0.5f, c.y - gap - len}, {t, len}, col);
        push_solid(view, {c.x - t * 0.5f, c.y + gap}, {t, len}, col);
    }

    // Health bar, bottom-left.
    {
        const glm::vec2 size{180.0f * scale, 14.0f * scale};
        const glm::vec2 pos{16.0f * scale, vp.y - size.y - 16.0f * scale};
        const float frac = state.max_hp > 0.0f ? std::clamp(state.hp / state.max_hp, 0.0f, 1.0f) : 0.0f;
        push_solid(view, pos - glm::vec2{2.0f * scale}, size + glm::vec2{4.0f * scale},
                   {0.0f, 0.0f, 0.0f, 0.6f});
        push_solid(view, pos, {size.x * frac, size.y},
                   {0.75f + 0.25f * (1.0f - frac), 0.15f + 0.45f * frac, 0.12f, 0.95f});
    }

    // Dash cooldown pip under the health bar.
    if (state.dash_cooldown01 > 0.0f) {
        const glm::vec2 size{60.0f * scale * (1.0f - state.dash_cooldown01), 4.0f * scale};
        push_solid(view, {16.0f * scale, vp.y - 8.0f * scale}, size, {0.8f, 0.8f, 1.0f, 0.8f});
    }

    // Death: dim the world (the text lives in the debug UI layer).
    if (state.dead) {
        push_solid(view, {0, 0}, vp, {0.0f, 0.0f, 0.0f, 0.55f});
    }
}

} // namespace ds
