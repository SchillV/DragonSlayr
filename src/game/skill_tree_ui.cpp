#include "game/skill_tree_ui.hpp"

#include "render/font.hpp"
#include "sim/progression.hpp"
#include "sim/stats.hpp"
#include "sim/world.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>

namespace ds {

namespace {

// Ember design tokens (matching menu.cpp).
constexpr glm::vec4 kGold{0.788f, 0.635f, 0.294f, 1.0f};
constexpr glm::vec4 kParchment{0.906f, 0.847f, 0.722f, 1.0f};
constexpr glm::vec4 kEmber{0.878f, 0.282f, 0.122f, 1.0f};
constexpr glm::vec4 kDim{0.553f, 0.518f, 0.471f, 1.0f};
constexpr glm::vec4 kLocked{0.32f, 0.29f, 0.25f, 0.9f};

float ui_scale(glm::vec2 vp) {
    return std::max(1.0f, std::round(vp.y / 360.0f));
}

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

// A→B connector as one center-rotated quad (the overlay rotation support).
OverlayQuad line_quad(glm::vec2 a, glm::vec2 b, float thickness, glm::vec4 color) {
    const glm::vec2 mid = (a + b) * 0.5f;
    const float len = glm::distance(a, b);
    OverlayQuad q;
    q.size = {len, thickness};
    q.pos = mid - q.size * 0.5f;
    q.rot = std::atan2(b.y - a.y, b.x - a.x);
    q.layer = 0.0f; // white overlay layer
    q.color = color;
    return q;
}

// What a node gives, in player language ("BLOODLUST" / "+2 STR").
std::string grant_text(const ContentDB& content, const SkillNodeDef& node) {
    if (!node.feat.empty()) {
        const int feat = content.find_feat(node.feat);
        return feat >= 0 ? upper(content.feats[static_cast<size_t>(feat)].name)
                         : upper(node.feat);
    }
    return std::format("+{} {}", node.attr_points, upper(stat_name(node.attr)));
}

} // namespace

std::vector<NodeVis> layout_tree(const SkillTreeDef& tree, glm::vec2 vp) {
    const size_t n = tree.nodes.size();
    std::vector<int> rank(n, -1);
    std::function<int(size_t)> rank_of = [&](size_t i) -> int {
        if (rank[i] >= 0) {
            return rank[i];
        }
        int r = 0;
        for (const int p : tree.nodes[i].prereq_idx) {
            r = std::max(r, rank_of(static_cast<size_t>(p)) + 1); // acyclic by loader contract
        }
        return rank[i] = r;
    };
    int max_rank = 0;
    for (size_t i = 0; i < n; ++i) {
        max_rank = std::max(max_rank, rank_of(i));
    }

    // Columns by rank across the middle band of the screen; rows spread
    // evenly per rank in node-index order.
    const float left = vp.x * 0.16f;
    const float right = vp.x * 0.84f;
    const float top = vp.y * 0.26f;
    const float bottom = vp.y * 0.82f;

    std::vector<int> rank_count(static_cast<size_t>(max_rank) + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        ++rank_count[static_cast<size_t>(rank[i])];
    }
    std::vector<int> rank_slot(static_cast<size_t>(max_rank) + 1, 0);

    std::vector<NodeVis> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const int r = rank[i];
        const int in_rank = rank_count[static_cast<size_t>(r)];
        const int slot = rank_slot[static_cast<size_t>(r)]++;
        NodeVis vis;
        vis.node = static_cast<int>(i);
        vis.rank = r;
        vis.pos.x = max_rank == 0
                        ? (left + right) * 0.5f
                        : left + (right - left) * static_cast<float>(r) / static_cast<float>(max_rank);
        vis.pos.y = in_rank == 1 ? (top + bottom) * 0.5f
                                 : top + (bottom - top) * static_cast<float>(slot) /
                                             static_cast<float>(in_rank - 1);
        out.push_back(vis);
    }
    return out;
}

void SkillTreeUi::open(const World& world, glm::vec2 viewport) {
    open_ = true;
    close_requested_ = false;
    viewport_ = viewport;
    tree_ = world.active_tree;
    selection_ = 0;
    nodes_.clear();
    if (tree_ >= 0 && static_cast<size_t>(tree_) < world.content.skill_trees.size()) {
        nodes_ = layout_tree(world.content.skill_trees[static_cast<size_t>(tree_)], viewport);
    }
}

void SkillTreeUi::move_selection(glm::vec2 dir) {
    if (nodes_.empty()) {
        return;
    }
    const glm::vec2 from = nodes_[static_cast<size_t>(selection_)].pos;
    int best = -1;
    float best_score = 0.0f;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (static_cast<int>(i) == selection_) {
            continue;
        }
        const glm::vec2 to = nodes_[i].pos - from;
        if (glm::dot(to, dir) <= 0.0f) {
            continue; // behind the direction of travel
        }
        // Distance, penalized for drifting off-axis.
        const float along = glm::dot(to, dir);
        const float off = std::abs(glm::dot(to, glm::vec2{-dir.y, dir.x}));
        const float score = along + 2.0f * off;
        if (best < 0 || score < best_score) {
            best = static_cast<int>(i);
            best_score = score;
        }
    }
    if (best >= 0) {
        selection_ = best;
    }
}

void SkillTreeUi::update(World& world, const MenuInput& in, glm::vec2 viewport) {
    if (!open_) {
        return;
    }
    if (viewport != viewport_ || tree_ != world.active_tree) {
        open(world, viewport); // relayout on resize / tree change
    }
    close_requested_ = false;
    if (in.back) {
        close_requested_ = true;
        return;
    }
    if (in.up) move_selection({0.0f, -1.0f});
    if (in.down) move_selection({0.0f, 1.0f});
    if (in.left) move_selection({-1.0f, 0.0f});
    if (in.right) move_selection({1.0f, 0.0f});

    if ((in.mouse_moved || in.click) && !nodes_.empty()) {
        const float grab = 26.0f * ui_scale(viewport_);
        int nearest = -1;
        float nearest_d2 = grab * grab;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const glm::vec2 d = nodes_[i].pos - in.mouse_px;
            const float d2 = glm::dot(d, d);
            if (d2 < nearest_d2) {
                nearest = static_cast<int>(i);
                nearest_d2 = d2;
            }
        }
        if (nearest >= 0) {
            selection_ = nearest;
            if (in.click) {
                purchase_node(world, nodes_[static_cast<size_t>(nearest)].node);
            }
        }
    }
    if (in.select && !nodes_.empty()) {
        purchase_node(world, nodes_[static_cast<size_t>(selection_)].node);
    }
}

void SkillTreeUi::render(FrameView& view, const FontAtlas& font, const World& world) const {
    if (!open_ || !font.valid()) {
        return;
    }
    const glm::vec2 vp = viewport_;
    const float scale = ui_scale(vp);

    auto solid = [&view](glm::vec2 pos, glm::vec2 size, glm::vec4 color, float rot = 0.0f) {
        OverlayQuad q;
        q.pos = pos;
        q.size = size;
        q.rot = rot;
        q.layer = 0.0f;
        q.color = color;
        view.overlay.push_back(q);
    };

    solid({0.0f, 0.0f}, vp, {0.02f, 0.015f, 0.01f, 0.78f}); // dim the world hard

    const bool has_tree =
        tree_ >= 0 && static_cast<size_t>(tree_) < world.content.skill_trees.size();
    const char* title = has_tree
                            ? world.content.skill_trees[static_cast<size_t>(tree_)].name.c_str()
                            : "NO TREE FOR THIS CLASS";
    emit_text(view.overlay_text, font, upper(title), {vp.x * 0.16f, vp.y * 0.09f}, 2.0f * scale,
              kParchment, 2.0f);
    emit_text(view.overlay_text, font,
              std::format("LV {}   XP {}/{}   POINTS {}", world.level,
                          static_cast<int>(world.xp), static_cast<int>(xp_to_next(world.level)),
                          world.skill_points),
              {vp.x * 0.16f, vp.y * 0.09f + 22.0f * scale}, scale,
              {kGold.r, kGold.g, kGold.b, 0.95f}, 1.0f);

    if (!has_tree) {
        return;
    }
    const SkillTreeDef& tree = world.content.skill_trees[static_cast<size_t>(tree_)];

    // Connectors underneath the nodes, colored by the child's state.
    for (const NodeVis& vis : nodes_) {
        const SkillNodeDef& node = tree.nodes[static_cast<size_t>(vis.node)];
        const NodeState state = node_state(world, vis.node);
        for (const int prereq : node.prereq_idx) {
            const auto parent = std::find_if(nodes_.begin(), nodes_.end(),
                                             [prereq](const NodeVis& v) { return v.node == prereq; });
            if (parent == nodes_.end()) {
                continue;
            }
            const glm::vec4 col = state == NodeState::Purchased
                                      ? glm::vec4{kGold.r, kGold.g, kGold.b, 0.8f}
                                  : state == NodeState::Available
                                      ? glm::vec4{kDim.r, kDim.g, kDim.b, 0.7f}
                                      : glm::vec4{kLocked.r, kLocked.g, kLocked.b, 0.5f};
            view.overlay.push_back(line_quad(parent->pos, vis.pos, 1.5f * scale, col));
        }
    }

    // Nodes: state-colored diamonds with the name (and cost) underneath.
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const NodeVis& vis = nodes_[i];
        const SkillNodeDef& node = tree.nodes[static_cast<size_t>(vis.node)];
        const NodeState state = node_state(world, vis.node);
        const bool selected = static_cast<int>(i) == selection_;
        const bool affordable = world.skill_points >= node.cost;

        glm::vec4 fill = state == NodeState::Purchased ? kGold
                         : state == NodeState::Available
                             ? (affordable ? kParchment
                                           : glm::vec4{kDim.r, kDim.g, kDim.b, 1.0f})
                             : kLocked;
        const float d = (selected ? 13.0f : 10.0f) * scale;
        if (selected) {
            solid(vis.pos - glm::vec2{d * 0.5f + 3.0f * scale},
                  glm::vec2{d + 6.0f * scale}, {kEmber.r, kEmber.g, kEmber.b, 0.9f},
                  glm::quarter_pi<float>());
        }
        solid(vis.pos - glm::vec2{d * 0.5f}, glm::vec2{d}, fill, glm::quarter_pi<float>());
        if (state == NodeState::Purchased) { // ember core marks owned nodes
            const float c = d * 0.4f;
            solid(vis.pos - glm::vec2{c * 0.5f}, glm::vec2{c}, kEmber, glm::quarter_pi<float>());
        }

        const std::string label = upper(node.name);
        const glm::vec2 lsize = measure_text(font, label, scale);
        emit_text(view.overlay_text, font, label,
                  {vis.pos.x - lsize.x * 0.5f, vis.pos.y + d * 0.5f + 5.0f * scale}, scale,
                  selected ? kParchment : glm::vec4{kDim.r, kDim.g, kDim.b, 0.9f});
    }

    // Selected node details, bottom-left: grant, cost, state.
    if (!nodes_.empty()) {
        const SkillNodeDef& node =
            tree.nodes[static_cast<size_t>(nodes_[static_cast<size_t>(selection_)].node)];
        const NodeState state = node_state(world, nodes_[static_cast<size_t>(selection_)].node);
        const float x = vp.x * 0.16f;
        float y = vp.y * 0.87f;
        emit_text(view.overlay_text, font, upper(node.name), {x, y}, scale, kParchment, 2.0f);
        y += font.line_advance * scale + 3.0f * scale;
        const char* verdict = state == NodeState::Purchased ? "OWNED"
                              : state == NodeState::Locked  ? "LOCKED"
                              : world.skill_points >= node.cost ? "ENTER TO LEARN"
                                                                : "NOT ENOUGH POINTS";
        emit_text(view.overlay_text, font,
                  std::format("{}   ·   COST {}   ·   {}", grant_text(world.content, node),
                              node.cost, verdict),
                  {x, y}, scale, {kGold.r, kGold.g, kGold.b, 0.9f}, 1.0f);
    }

    emit_text(view.overlay_text, font, "ARROWS MOVE · ENTER LEARN · ESC CLOSE",
              {vp.x * 0.16f, vp.y - 16.0f * scale}, scale * 0.9f,
              {kDim.r, kDim.g, kDim.b, 0.55f}, 1.0f);
}

} // namespace ds
