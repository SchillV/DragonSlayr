#pragma once

#include "game/menu.hpp" // MenuInput
#include "render/frame_view.hpp"
#include "sim/stats.hpp"

#include <string>
#include <vector>

namespace ds {

struct FontAtlas;
struct World;

// Player-language derivation lines for one attribute at `points`
// ("+20% MELEE DAMAGE"); empty for non-attribute stat ids. Pure, unit-tested
// so the sheet can never drift from the recompute() table silently.
std::vector<std::string> describe_attr(StatId attr, float points);

// The CHARACTER page: a read-only sheet of attributes (with their derived
// effects), final effect stats, feats and relics. Same conventions as the
// tree page: SDL-free, MenuInput in, overlay quads/text out.
class StatsUi {
public:
    void open() { open_ = true; close_requested_ = false; }
    void close() { open_ = false; }
    bool active() const { return open_; }
    bool close_requested() const { return close_requested_; }

    void update(const MenuInput& in);
    void render(FrameView& view, const FontAtlas& font, const World& world,
                glm::vec2 viewport) const;

private:
    bool open_ = false;
    bool close_requested_ = false;
};

} // namespace ds
