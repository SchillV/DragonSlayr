#include "game/hud.hpp"

#include "render/font.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

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

float indicator_screen_rot(float attack_world_angle, float cam_yaw) {
    const float two_pi = glm::two_pi<float>();
    float d = std::fmod(attack_world_angle - cam_yaw + glm::pi<float>(), two_pi);
    if (d < 0.0f) {
        d += two_pi;
    }
    return d - glm::pi<float>();
}

void build_hud(FrameView& view, const HudState& state, glm::vec2 vp, const FontAtlas* font) {
    const float scale = std::max(1.0f, std::round(vp.y / 360.0f)); // retro pixel scale
    const float hp_frac =
        state.max_hp > 0.0f ? std::clamp(state.hp / state.max_hp, 0.0f, 1.0f) : 0.0f;

    // Hurt flash (under everything else so the HUD stays readable).
    if (state.hurt_flash > 0.0f) {
        push_solid(view, {0, 0}, vp, {0.8f, 0.05f, 0.05f, state.hurt_flash * 0.35f});
    }

    // Low-health warning: pulsing red border, faster and stronger as HP drops.
    if (!state.dead && hp_frac < state.lowhp_threshold && state.lowhp_threshold > 0.0f) {
        const float severity = 1.0f - hp_frac / state.lowhp_threshold; // 0..1
        const float pulse_hz = 1.2f + 2.2f * severity;
        const float pulse =
            0.6f + 0.4f * static_cast<float>(std::sin(state.time * pulse_hz * glm::two_pi<float>()));
        const float alpha = (0.18f + 0.30f * severity) * pulse;
        const glm::vec4 col{0.75f, 0.05f, 0.05f, alpha};
        const float th = vp.y * 0.045f;
        push_solid(view, {0, 0}, {vp.x, th}, col);
        push_solid(view, {0, vp.y - th}, {vp.x, th}, col);
        push_solid(view, {0, th}, {th, vp.y - 2.0f * th}, col);
        push_solid(view, {vp.x - th, th}, {th, vp.y - 2.0f * th}, col);
    }

    // Directional damage indicators: radial bars around the crosshair pointing
    // at whoever hit you; they track as you turn and fade out.
    for (const DamageIndicator& ind : state.indicators) {
        if (ind.t <= 0.0f) {
            continue;
        }
        const float rot = indicator_screen_rot(ind.world_angle, state.cam_yaw);
        const float radius = 90.0f * scale;
        const glm::vec2 center = vp * 0.5f;
        const glm::vec2 dir{std::sin(rot), -std::cos(rot)}; // rot 0 = up
        const glm::vec2 size{10.0f * scale, 26.0f * scale};
        OverlayQuad q;
        q.pos = center + dir * radius - size * 0.5f;
        q.size = size;
        q.rot = rot;
        q.layer = kOverlayWhite;
        q.color = {0.92f, 0.12f, 0.08f, 0.85f * std::min(ind.t * 2.0f, 1.0f)};
        view.overlay.push_back(q);
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

    // Hitmarker: an X of four diagonal ticks when your hit lands; kills get a
    // bigger, red, slower-fading variant.
    if (state.hitmarker_t > 0.0f) {
        const glm::vec2 c = vp * 0.5f;
        const float boost = state.hitmarker_kill ? 1.6f : 1.0f;
        const glm::vec4 col = state.hitmarker_kill
                                  ? glm::vec4{0.95f, 0.15f, 0.1f, 0.95f * state.hitmarker_t}
                                  : glm::vec4{1.0f, 1.0f, 1.0f, 0.9f * state.hitmarker_t};
        const float r = (8.0f + 3.0f * (1.0f - state.hitmarker_t)) * scale * boost;
        const glm::vec2 size{2.5f * scale * boost, 7.0f * scale * boost};
        for (int i = 0; i < 4; ++i) {
            const float a = glm::quarter_pi<float>() + glm::half_pi<float>() * static_cast<float>(i);
            OverlayQuad q;
            q.pos = c + glm::vec2{std::sin(a), -std::cos(a)} * r - size * 0.5f;
            q.size = size;
            q.rot = a;
            q.layer = kOverlayWhite;
            q.color = col;
            view.overlay.push_back(q);
        }
    }

    // Health bar, bottom-left, with a white "chip" showing damage just taken.
    {
        const glm::vec2 size{180.0f * scale, 14.0f * scale};
        const glm::vec2 pos{16.0f * scale, vp.y - size.y - 16.0f * scale};
        const float frac = hp_frac;
        push_solid(view, pos - glm::vec2{2.0f * scale}, size + glm::vec2{4.0f * scale},
                   {0.0f, 0.0f, 0.0f, 0.6f});

        const float chip_frac =
            state.max_hp > 0.0f ? std::clamp(state.chip_hp / state.max_hp, 0.0f, 1.0f) : 0.0f;
        if (chip_frac > frac) {
            push_solid(view, {pos.x + size.x * frac, pos.y}, {size.x * (chip_frac - frac), size.y},
                       {0.95f, 0.9f, 0.85f, 0.85f});
        }

        float bar_alpha = 0.95f;
        if (!state.dead && frac < state.lowhp_threshold) {
            bar_alpha = 0.65f + 0.3f * static_cast<float>(
                                           std::sin(state.time * 6.0 * glm::two_pi<double>()) * 0.5 + 0.5);
        }
        push_solid(view, pos, {size.x * frac, size.y},
                   {0.75f + 0.25f * (1.0f - frac), 0.15f + 0.45f * frac, 0.12f, bar_alpha});

        if (font && font->valid()) {
            const std::string hp_text =
                std::format("{}/{}", static_cast<int>(std::ceil(state.hp)),
                            static_cast<int>(state.max_hp));
            const float ts = scale; // 8px glyphs at the HUD pixel scale
            const glm::vec2 tsize = measure_text(*font, hp_text, ts);
            emit_text(view.overlay_text, *font, hp_text,
                      {pos.x + size.x + 8.0f * scale, pos.y + (size.y - tsize.y) * 0.5f}, ts,
                      {0.95f, 0.92f, 0.82f, 0.95f});
        }

        // XP sliver + level under the health bar; a hint when points wait.
        const float xp_y = pos.y + size.y + 3.0f * scale;
        push_solid(view, {pos.x, xp_y}, {size.x, 2.0f * scale}, {0.0f, 0.0f, 0.0f, 0.55f});
        push_solid(view, {pos.x, xp_y}, {size.x * std::clamp(state.xp01, 0.0f, 1.0f), 2.0f * scale},
                   {0.79f, 0.64f, 0.29f, 0.9f});
        if (font && font->valid()) {
            emit_text(view.overlay_text, *font, std::format("LV {}", state.level),
                      {pos.x, xp_y + 4.0f * scale}, scale, {0.79f, 0.64f, 0.29f, 0.85f}, 1.0f);
            if (state.skill_points > 0 && !state.dead) {
                const float pulse =
                    0.6f + 0.4f * static_cast<float>(
                                      std::sin(state.time * 2.0 * glm::two_pi<double>()) * 0.5 + 0.5);
                emit_text(view.overlay_text, *font,
                          std::format("+{} POINT{} · T", state.skill_points,
                                      state.skill_points > 1 ? "S" : ""),
                          {pos.x + 42.0f * scale, xp_y + 4.0f * scale}, scale,
                          {0.91f, 0.85f, 0.69f, pulse}, 1.0f);
            }
        }
    }

    // Active feats, bottom-right: diamond + "NAME xN" chips per the design's
    // bottom bar, laid right-to-left.
    if (font && font->valid() && !state.feats.empty()) {
        float x = vp.x - 14.0f * scale;
        const float y = vp.y - 24.0f * scale;
        const size_t shown = std::min(state.feats.size(), size_t{6});
        for (size_t i = 0; i < shown; ++i) {
            const FeatChip& chip = state.feats[i];
            std::string label = chip.name;
            for (char& c : label) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (chip.count > 1) {
                label += std::format(" x{}", chip.count);
            }
            const glm::vec2 tsize = measure_text(*font, label, scale, 1.0f);
            x -= tsize.x;
            emit_text(view.overlay_text, *font, label, {x, y}, scale,
                      {0.79f, 0.64f, 0.29f, 0.9f}, 1.0f);
            const float d = 4.0f * scale;
            OverlayQuad q;
            q.pos = {x - 8.0f * scale, y + (tsize.y - d) * 0.5f - scale};
            q.size = {d, d};
            q.rot = glm::quarter_pi<float>();
            q.layer = kOverlayWhite;
            q.color = {0.88f, 0.28f, 0.12f, 0.9f};
            view.overlay.push_back(q);
            x -= 22.0f * scale; // diamond + gap before the next chip
        }
    }

    // Score + floor, top-right.
    if (font && font->valid()) {
        const std::string score_text = std::format("SCORE {}", state.score);
        const float ts = scale;
        const glm::vec2 tsize = measure_text(*font, score_text, ts);
        emit_text(view.overlay_text, *font, score_text,
                  {vp.x - tsize.x - 16.0f * scale, 12.0f * scale}, ts,
                  {0.91f, 0.85f, 0.69f, 0.9f});
        const std::string floor_text = std::format("FLOOR {}", state.floor);
        const glm::vec2 fsize = measure_text(*font, floor_text, ts);
        emit_text(view.overlay_text, *font, floor_text,
                  {vp.x - fsize.x - 16.0f * scale, 12.0f * scale + font->line_advance * ts + 2.0f * scale},
                  ts, {0.79f, 0.64f, 0.29f, 0.85f});
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
