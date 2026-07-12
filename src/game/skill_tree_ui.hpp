#pragma once

#include "game/menu.hpp" // MenuInput
#include "render/frame_view.hpp"
#include "sim/content.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace ds {

struct FontAtlas;
struct World;

// Where a tree node landed on screen. Produced by layout_tree — the whole
// page is derived from the data: columns from prerequisite depth, rows from
// rank population, connectors from the `requires` edges.
struct NodeVis {
    int node = 0; // index into SkillTreeDef::nodes
    glm::vec2 pos{0.0f};
    int rank = 0;
};

// Pure auto-layout: rank = longest prerequisite chain, x by rank, nodes of a
// rank spread vertically in node-index order (stable across reloads).
std::vector<NodeVis> layout_tree(const SkillTreeDef& tree, glm::vec2 viewport);

// The SKILL TREE page. SDL-free like MenuSystem: the app feeds MenuInput and
// draws through the overlay pipeline; purchods go straight to the sim.
class SkillTreeUi {
public:
    void open(const World& world, glm::vec2 viewport);
    void close() { open_ = false; }
    bool active() const { return open_; }
    bool close_requested() const { return close_requested_; }

    void update(World& world, const MenuInput& in, glm::vec2 viewport);
    void render(FrameView& view, const FontAtlas& font, const World& world) const;

    std::span<const NodeVis> nodes() const { return nodes_; }
    int selection() const { return selection_; } // index into nodes()

private:
    void move_selection(glm::vec2 dir);

    std::vector<NodeVis> nodes_;
    int selection_ = 0;
    int tree_ = -1;
    bool open_ = false;
    bool close_requested_ = false;
    glm::vec2 viewport_{1280.0f, 720.0f};
};

} // namespace ds
