#include "game/menu.hpp"

#include "core/cvar.hpp"
#include "render/font.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <format>

namespace ds {

namespace {

// Ember design tokens (from the owner's UI wireframe).
constexpr glm::vec4 kGold{0.788f, 0.635f, 0.294f, 1.0f};      // #c9a24b
constexpr glm::vec4 kParchment{0.906f, 0.847f, 0.722f, 1.0f}; // #e7d8b8
constexpr glm::vec4 kEmber{0.878f, 0.282f, 0.122f, 1.0f};     // #e0481f
constexpr glm::vec4 kDim{0.553f, 0.518f, 0.471f, 1.0f};       // #8d8478
constexpr glm::vec4 kDisabled{0.42f, 0.38f, 0.32f, 0.75f};
constexpr glm::vec4 kPanel{0.078f, 0.063f, 0.047f, 0.94f};    // #141008

constexpr const char* kBuildLine = "V0.5 · THE EMBER BUILD · © SCHILLV";

float ui_scale(glm::vec2 vp) {
    return std::max(1.0f, std::round(vp.y / 360.0f));
}

const char* screen_title(MenuScreen s) {
    switch (s) {
    case MenuScreen::Title: return "DRAGONSLAYR";
    case MenuScreen::Pause: return "PAUSED";
    case MenuScreen::Settings: return "OPTIONS";
    case MenuScreen::Death: return "YOU DIED";
    case MenuScreen::ClassSelect: return "CLASS & FEATS";
    }
    return "";
}

// Title uses the design's left-column layout; the in-game screens are
// centered panels over the dimmed world.
struct Layout {
    glm::vec2 list_origin{0.0f};
    float item_h = 0.0f;
    float item_w = 0.0f;
    glm::vec2 header_pos{0.0f};
};

Layout layout_for(MenuScreen s, size_t item_count, glm::vec2 vp) {
    const float scale = ui_scale(vp);
    Layout l;
    l.item_h = 22.0f * scale;
    l.item_w = 240.0f * scale;
    if (s == MenuScreen::Title) {
        l.header_pos = {vp.x * 0.14f, vp.y * 0.22f};
        l.list_origin = {vp.x * 0.14f, vp.y * 0.46f};
    } else {
        const float list_h = static_cast<float>(item_count) * l.item_h;
        l.list_origin = {(vp.x - l.item_w) * 0.5f, (vp.y - list_h) * 0.55f};
        l.header_pos = {l.list_origin.x, l.list_origin.y - 58.0f * scale};
    }
    return l;
}

MenuItem button(std::string label, MenuAction action, bool destructive = false) {
    MenuItem it;
    it.label = std::move(label);
    it.action = action;
    it.destructive = destructive;
    return it;
}

MenuItem submenu(std::string label, MenuScreen target) {
    MenuItem it;
    it.label = std::move(label);
    it.push_screen = static_cast<int>(target);
    return it;
}

MenuItem placeholder(std::string label) {
    MenuItem it;
    it.label = std::move(label);
    it.enabled = false;
    return it;
}

MenuItem slider(std::string label, std::string cvar, float min, float max, float step,
                bool integer = false) {
    MenuItem it;
    it.label = std::move(label);
    it.kind = MenuItem::Kind::Slider;
    it.cvar = std::move(cvar);
    it.min = min;
    it.max = max;
    it.step = step;
    it.integer = integer;
    return it;
}

} // namespace

void MenuSystem::open(MenuScreen root) {
    stack_ = {root};
    status_line_.clear();
    rebuild_items();
    select_first_enabled();
}

void MenuSystem::close() {
    stack_.clear();
    items_.clear();
    status_line_.clear();
}

void MenuSystem::rebuild_items() {
    items_.clear();
    if (stack_.empty()) {
        return;
    }
    switch (current()) {
    case MenuScreen::Title:
        // Page structure from the design: NEW GAME / CONTINUE / CLASS & FEATS /
        // LEADERBOARD / OPTIONS / ABANDON. Systems that don't exist yet are
        // honest placeholders.
        items_.push_back(button("NEW GAME", MenuAction::StartRun));
        items_.push_back(placeholder("CONTINUE · SOON"));
        items_.push_back(submenu("CLASS & FEATS", MenuScreen::ClassSelect));
        items_.push_back(placeholder("LEADERBOARD · SOON"));
        items_.push_back(submenu("OPTIONS", MenuScreen::Settings));
        items_.push_back(button("ABANDON", MenuAction::QuitGame, /*destructive=*/true));
        break;
    case MenuScreen::ClassSelect:
        for (const RosterEntry& entry : class_roster_) {
            MenuItem it;
            it.label = entry.payload == roster_current_ ? entry.label + "  · CHOSEN"
                                                        : entry.label;
            it.blurb = entry.blurb;
            it.action = MenuAction::SelectClass;
            it.payload = entry.payload;
            items_.push_back(std::move(it));
        }
        if (items_.empty()) {
            items_.push_back(placeholder("NO CLASSES DEFINED"));
        }
        items_.push_back(button("BACK", MenuAction::None)); // pops via activate()
        break;
    case MenuScreen::Pause:
        items_.push_back(button("RESUME", MenuAction::Resume));
        items_.push_back(button("RESTART RUN", MenuAction::Restart));
        items_.push_back(submenu("OPTIONS", MenuScreen::Settings));
        items_.push_back(button("ABANDON RUN", MenuAction::QuitToTitle, /*destructive=*/true));
        break;
    case MenuScreen::Settings: {
        const MenuItem candidates[] = {
            slider("FIELD OF VIEW", "r.fov", 66.0f, 110.0f, 2.0f, /*integer=*/true),
            slider("MOUSE SENSITIVITY", "in.sensitivity", 0.4f, 6.0f, 0.2f),
            slider("VOLUME", "snd.volume", 0.0f, 1.0f, 0.05f),
            slider("SCREEN SHAKE", "fx.shake", 0.0f, 2.0f, 0.1f),
        };
        for (const MenuItem& c : candidates) {
            if (cvar_find(c.cvar)) { // sliders bind live cvars only
                items_.push_back(c);
            }
        }
        items_.push_back(button("BACK", MenuAction::None)); // pops via activate()
        break;
    }
    case MenuScreen::Death:
        items_.push_back(button("DELVE AGAIN", MenuAction::Restart));
        items_.push_back(button("RETURN TO TITLE", MenuAction::QuitToTitle));
        break;
    }
}

void MenuSystem::select_first_enabled() {
    selection_ = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].enabled) {
            selection_ = i;
            return;
        }
    }
}

void MenuSystem::move_selection(int dir) {
    if (items_.empty()) {
        return;
    }
    const size_t n = items_.size();
    size_t i = selection_;
    for (size_t steps = 0; steps < n; ++steps) {
        i = (i + n + static_cast<size_t>(dir)) % n;
        if (items_[i].enabled) {
            selection_ = i;
            return;
        }
    }
}

glm::vec4 MenuSystem::item_rect(size_t index) const {
    const Layout l = layout_for(stack_.empty() ? MenuScreen::Title : current(), items_.size(),
                                viewport_);
    return {l.list_origin.x, l.list_origin.y + static_cast<float>(index) * l.item_h, l.item_w,
            l.item_h};
}

MenuAction MenuSystem::activate(size_t index) {
    MenuItem& it = items_[index];
    if (!it.enabled) {
        return MenuAction::None;
    }
    if (it.kind == MenuItem::Kind::Slider) {
        return MenuAction::None; // sliders adjust with left/right, not select
    }
    if (it.push_screen >= 0) {
        stack_.push_back(static_cast<MenuScreen>(it.push_screen));
        rebuild_items();
        select_first_enabled();
        return MenuAction::None;
    }
    if ((current() == MenuScreen::Settings || current() == MenuScreen::ClassSelect) &&
        it.action == MenuAction::None) {
        // BACK
        stack_.pop_back();
        rebuild_items();
        select_first_enabled();
        return MenuAction::None;
    }
    if (it.action == MenuAction::SelectClass) {
        chosen_payload_ = it.payload;
        roster_current_ = it.payload;
        const MenuAction action = it.action;
        rebuild_items(); // the CHOSEN marker moves immediately (invalidates `it`)
        return action;
    }
    return it.action;
}

void MenuSystem::adjust_slider(size_t index, int dir) {
    MenuItem& it = items_[index];
    if (it.kind != MenuItem::Kind::Slider) {
        return;
    }
    if (CVar* cv = cvar_find(it.cvar)) {
        cvar_set(*cv, std::clamp(cv->value + static_cast<float>(dir) * it.step, it.min, it.max));
    }
}

MenuAction MenuSystem::update(const MenuInput& in, glm::vec2 viewport) {
    if (!active()) {
        return MenuAction::None;
    }
    viewport_ = viewport;
    rebuild_items(); // cheap; keeps slider values live and screens fresh
    if (selection_ >= items_.size() || !items_[selection_].enabled) {
        select_first_enabled();
    }

    if (in.back) {
        if (stack_.size() > 1) {
            stack_.pop_back();
            rebuild_items();
            select_first_enabled();
            return MenuAction::None;
        }
        if (current() == MenuScreen::Pause) {
            return MenuAction::Resume; // Esc on the pause root resumes
        }
        return MenuAction::None;
    }

    if (in.up) {
        move_selection(-1);
    }
    if (in.down) {
        move_selection(+1);
    }
    if (in.left) {
        adjust_slider(selection_, -1);
    }
    if (in.right) {
        adjust_slider(selection_, +1);
    }

    if (in.mouse_moved || in.click) {
        for (size_t i = 0; i < items_.size(); ++i) {
            const glm::vec4 r = item_rect(i);
            const bool inside = in.mouse_px.x >= r.x && in.mouse_px.x <= r.x + r.z &&
                                in.mouse_px.y >= r.y && in.mouse_px.y <= r.y + r.w;
            if (inside && items_[i].enabled) {
                selection_ = i;
                if (in.click) {
                    return activate(i);
                }
            }
        }
    }

    if (in.select) {
        return activate(selection_);
    }
    return MenuAction::None;
}

void MenuSystem::render(FrameView& view, const FontAtlas& font) const {
    if (!active() || !font.valid()) {
        return;
    }
    const glm::vec2 vp = viewport_;
    const float scale = ui_scale(vp);
    const MenuScreen screen = current();
    const Layout l = layout_for(screen, items_.size(), vp);

    auto solid = [&view](glm::vec2 pos, glm::vec2 size, glm::vec4 color) {
        OverlayQuad q;
        q.pos = pos;
        q.size = size;
        q.layer = 0.0f; // white overlay layer
        q.color = color;
        view.overlay.push_back(q);
    };

    // Backdrop: the world stays visible but recedes.
    solid({0.0f, 0.0f}, vp, {0.02f, 0.015f, 0.01f, screen == MenuScreen::Title ? 0.62f : 0.55f});
    if (screen != MenuScreen::Title) {
        // Centered panel with a gold border, per the design language.
        const glm::vec2 pad{26.0f * scale, 30.0f * scale};
        const glm::vec2 panel_pos{l.list_origin.x - pad.x, l.header_pos.y - pad.y};
        const glm::vec2 panel_size{l.item_w + pad.x * 2.0f,
                                   (l.list_origin.y - l.header_pos.y) +
                                       static_cast<float>(items_.size()) * l.item_h +
                                       pad.y * 2.0f};
        solid(panel_pos, panel_size, kPanel);
        const float bw = 1.5f * scale;
        const glm::vec4 border{kGold.r, kGold.g, kGold.b, 0.55f};
        solid(panel_pos, {panel_size.x, bw}, border);
        solid({panel_pos.x, panel_pos.y + panel_size.y - bw}, {panel_size.x, bw}, border);
        solid(panel_pos, {bw, panel_size.y}, border);
        solid({panel_pos.x + panel_size.x - bw, panel_pos.y}, {bw, panel_size.y}, border);
    }

    // Header block.
    if (screen == MenuScreen::Title) {
        emit_text(view.overlay_text, font, "SLAY THE WYRM · AGAIN",
                  {l.header_pos.x, l.header_pos.y - 16.0f * scale}, scale,
                  {kDim.r, kDim.g, kDim.b, 0.9f}, 3.0f);
    }
    const glm::vec4 title_col = screen == MenuScreen::Death ? kEmber : kParchment;
    emit_text(view.overlay_text, font, screen_title(screen), l.header_pos, 3.0f * scale,
              title_col, 1.0f);
    if (!status_line_.empty()) {
        emit_text(view.overlay_text, font, status_line_,
                  {l.header_pos.x, l.header_pos.y + 28.0f * scale}, scale,
                  {kGold.r, kGold.g, kGold.b, 0.9f}, 1.0f);
    }

    // Items.
    for (size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        const glm::vec4 r = item_rect(i);
        const bool selected = i == selection_;

        glm::vec4 col = it.enabled ? (it.destructive ? kEmber : kParchment) : kDisabled;
        if (selected) {
            col = it.destructive ? kEmber : kGold;
        } else if (it.enabled && !it.destructive) {
            col = {kDim.r * 1.25f, kDim.g * 1.25f, kDim.b * 1.25f, 1.0f};
        }

        // Diamond bullet (the rotated-quad support earning its keep).
        {
            OverlayQuad q;
            const float d = 5.0f * scale;
            q.pos = {r.x, r.y + (r.w - d) * 0.5f};
            q.size = {d, d};
            q.rot = glm::quarter_pi<float>();
            q.layer = 0.0f;
            q.color = selected ? kEmber : glm::vec4{col.r, col.g, col.b, 0.7f};
            view.overlay.push_back(q);
        }

        const glm::vec2 text_pos{r.x + 14.0f * scale,
                                 r.y + (r.w - font.line_advance * scale) * 0.5f};
        emit_text(view.overlay_text, font, it.label, text_pos, scale, col, 2.0f);

        if (it.kind == MenuItem::Kind::Slider) {
            if (const CVar* cv = cvar_find(it.cvar)) {
                const float track_w = 70.0f * scale;
                const float track_h = 4.0f * scale;
                const glm::vec2 tpos{r.x + r.z - track_w - 34.0f * scale,
                                     r.y + (r.w - track_h) * 0.5f};
                solid(tpos, {track_w, track_h}, {0.0f, 0.0f, 0.0f, 0.6f});
                const float frac =
                    it.max > it.min ? std::clamp((cv->value - it.min) / (it.max - it.min), 0.0f, 1.0f)
                                    : 0.0f;
                solid(tpos, {track_w * frac, track_h},
                      selected ? kGold : glm::vec4{kDim.r, kDim.g, kDim.b, 0.9f});
                const std::string value = it.integer
                                              ? std::format("{}", static_cast<int>(cv->value))
                                              : std::format("{:.1f}", cv->value);
                emit_text(view.overlay_text, font, value,
                          {tpos.x + track_w + 6.0f * scale, text_pos.y}, scale, kParchment);
            }
        }

        if (selected) {
            // Gold underline, as in the design's selected item.
            solid({r.x + 14.0f * scale, r.y + r.w - 3.0f * scale},
                  {measure_text(font, it.label, scale, 2.0f).x, 1.5f * scale},
                  {kGold.r, kGold.g, kGold.b, 0.8f});
        }
    }

    // Selected item's blurb (class descriptions et al.) under the list.
    if (selection_ < items_.size() && !items_[selection_].blurb.empty()) {
        const glm::vec2 pos{l.list_origin.x,
                            l.list_origin.y + static_cast<float>(items_.size()) * l.item_h +
                                10.0f * scale};
        emit_text(view.overlay_text, font, items_[selection_].blurb, pos, scale,
                  {kDim.r * 1.15f, kDim.g * 1.15f, kDim.b * 1.15f, 0.95f}, 1.0f);
    }

    // Footer.
    emit_text(view.overlay_text, font, kBuildLine,
              {l.header_pos.x, vp.y - 22.0f * scale}, scale * 0.9f,
              {kDim.r, kDim.g, kDim.b, 0.55f}, 1.0f);
}

} // namespace ds
