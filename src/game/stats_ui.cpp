#include "game/stats_ui.hpp"

#include "render/font.hpp"
#include "sim/components.hpp"
#include "sim/progression.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace ds {

namespace {

constexpr glm::vec4 kGold{0.788f, 0.635f, 0.294f, 1.0f};
constexpr glm::vec4 kParchment{0.906f, 0.847f, 0.722f, 1.0f};
constexpr glm::vec4 kEmber{0.878f, 0.282f, 0.122f, 1.0f};
constexpr glm::vec4 kDim{0.553f, 0.518f, 0.471f, 1.0f};

float ui_scale(glm::vec2 vp) {
    return std::max(1.0f, std::round(vp.y / 360.0f));
}

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string signed_pct(float frac) {
    return std::format("{}{}%", frac >= 0.0f ? "+" : "", static_cast<int>(std::round(frac * 100.0f)));
}

} // namespace

std::vector<std::string> describe_attr(StatId attr, float points) {
    // Mirrors the derivation table in StatBlock::recompute(); keep in step.
    switch (attr) {
    case StatId::Str: return {std::format("{} MELEE DAMAGE", signed_pct(0.05f * points))};
    case StatId::Mag: return {std::format("{} MAGIC DAMAGE", signed_pct(0.05f * points))};
    case StatId::Dex:
        return {std::format("{} ATTACK SPEED", signed_pct(0.03f * points)),
                std::format("{} MOVE SPEED", signed_pct(0.02f * points))};
    case StatId::Vit:
        return {std::format("{}{} MAX HEALTH", points >= 0.0f ? "+" : "",
                            static_cast<int>(std::round(8.0f * points))),
                std::format("{} DEFENSE", signed_pct(0.01f * points))};
    default: return {};
    }
}

void StatsUi::update(const MenuInput& in) {
    if (!open_) {
        return;
    }
    close_requested_ = in.back || in.select; // any commit key closes the sheet
}

void StatsUi::render(FrameView& view, const FontAtlas& font, const World& world,
                     glm::vec2 vp) const {
    if (!open_ || !font.valid()) {
        return;
    }
    const float scale = ui_scale(vp);

    OverlayQuad dim;
    dim.pos = {0.0f, 0.0f};
    dim.size = vp;
    dim.layer = 0.0f;
    dim.color = {0.02f, 0.015f, 0.01f, 0.8f};
    view.overlay.push_back(dim);

    // Header: class, level, xp.
    const char* class_name = "UNBOUND";
    if (world.selected_class >= 0 &&
        static_cast<size_t>(world.selected_class) < world.content.classes.size()) {
        class_name = world.content.classes[static_cast<size_t>(world.selected_class)].name.c_str();
    }
    emit_text(view.overlay_text, font, "CHARACTER", {vp.x * 0.16f, vp.y * 0.09f}, 2.0f * scale,
              kParchment, 2.0f);
    emit_text(view.overlay_text, font,
              std::format("{}   ·   LV {}   ·   XP {}/{}", upper(class_name), world.level,
                          static_cast<int>(world.xp), static_cast<int>(xp_to_next(world.level))),
              {vp.x * 0.16f, vp.y * 0.09f + 22.0f * scale}, scale,
              {kGold.r, kGold.g, kGold.b, 0.95f}, 1.0f);

    const Stats& s = world.reg.get<StatBlock>(world.player).cached;

    // Left column: the classic sheet with its derivations spelled out.
    {
        const float x = vp.x * 0.16f;
        float y = vp.y * 0.27f;
        const StatId attrs[] = {StatId::Str, StatId::Dex, StatId::Vit, StatId::Mag};
        for (const StatId id : attrs) {
            emit_text(view.overlay_text, font,
                      std::format("{}  {}", upper(stat_name(id)), static_cast<int>(s[id])),
                      {x, y}, 2.0f * scale, kParchment, 1.0f);
            y += font.line_advance * 2.0f * scale + 2.0f * scale;
            for (const std::string& line : describe_attr(id, s[id])) {
                emit_text(view.overlay_text, font, line, {x + 8.0f * scale, y}, scale,
                          {kDim.r * 1.2f, kDim.g * 1.2f, kDim.b * 1.2f, 0.95f}, 1.0f);
                y += font.line_advance * scale + 2.0f * scale;
            }
            y += 8.0f * scale;
        }
    }

    // Right column: the final numbers the sim actually uses.
    {
        const float x = vp.x * 0.55f;
        float y = vp.y * 0.27f;
        const auto& hp = world.reg.get<Health>(world.player);
        const std::pair<std::string, std::string> rows[] = {
            {"HEALTH", std::format("{}/{}", static_cast<int>(std::ceil(hp.hp)),
                                   static_cast<int>(hp.max_hp))},
            {"MELEE DAMAGE", std::format("x{:.2f}", s.damage_mult * s.melee_damage_mult)},
            {"MAGIC DAMAGE", std::format("x{:.2f}", s.damage_mult * s.magic_damage_mult)},
            {"ATTACK SPEED", std::format("x{:.2f}", s.fire_rate_mult)},
            {"MOVE SPEED", std::format("x{:.2f}", s.move_speed_mult)},
            {"DEFENSE", std::format("{}%", static_cast<int>(std::round(s.defense_pct * 100.0f)))},
        };
        for (const auto& [label, value] : rows) {
            emit_text(view.overlay_text, font, label, {x, y}, scale,
                      {kGold.r, kGold.g, kGold.b, 0.9f}, 1.0f);
            const glm::vec2 vsize = measure_text(font, value, scale);
            emit_text(view.overlay_text, font, value, {x + 170.0f * scale - vsize.x, y}, scale,
                      kParchment);
            y += font.line_advance * scale + 6.0f * scale;
        }
    }

    // Feats and relics along the bottom.
    {
        const float x = vp.x * 0.16f;
        float y = vp.y * 0.80f;
        std::string feats_line = "FEATS  ";
        if (const auto* set = world.reg.try_get<FeatSet>(world.player);
            set && !set->entries.empty()) {
            for (size_t i = 0; i < set->entries.size() && i < 6; ++i) {
                const FeatSet::Entry& e = set->entries[i];
                feats_line += upper(world.content.feats[e.feat].name);
                if (e.count > 1) {
                    feats_line += std::format(" x{}", e.count);
                }
                feats_line += "   ";
            }
        } else {
            feats_line += "NONE YET";
        }
        emit_text(view.overlay_text, font, feats_line, {x, y}, scale,
                  {kEmber.r, kEmber.g, kEmber.b, 0.9f}, 1.0f);
        y += font.line_advance * scale + 6.0f * scale;

        std::string relics_line = "RELICS  ";
        if (const auto* inv = world.reg.try_get<Inventory>(world.player);
            inv && !inv->items.empty()) {
            for (size_t i = 0; i < inv->items.size() && i < 6; ++i) {
                relics_line += upper(world.content.items[inv->items[i]].name) + "   ";
            }
            if (inv->items.size() > 6) {
                relics_line += std::format("+{} MORE", inv->items.size() - 6);
            }
        } else {
            relics_line += "NONE YET";
        }
        emit_text(view.overlay_text, font, relics_line, {x, y}, scale,
                  {kGold.r, kGold.g, kGold.b, 0.85f}, 1.0f);
    }

    emit_text(view.overlay_text, font, "ESC CLOSE", {vp.x * 0.16f, vp.y - 16.0f * scale},
              scale * 0.9f, {kDim.r, kDim.g, kDim.b, 0.55f}, 1.0f);
}

} // namespace ds
