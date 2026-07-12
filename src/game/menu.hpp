#pragma once

#include "render/frame_view.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ds {

struct FontAtlas;

// The player-facing menu framework. Deliberately SDL-free: the app translates
// events into MenuInput and executes the returned MenuAction, so the whole
// state machine is unit-testable and the screens render through the same
// overlay pipeline as the HUD.

enum class MenuScreen : uint8_t { Title, Pause, Settings, Death, ClassSelect };

enum class MenuAction : uint8_t {
    None,
    StartRun,
    Resume,
    Restart,
    QuitToTitle,
    QuitGame,
    SelectClass, // read the choice with chosen_payload()
    OpenTree,    // the skill-tree page (owned by the app, not MenuSystem)
};

// Edge-triggered navigation input for one frame.
struct MenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool select = false;
    bool back = false;
    glm::vec2 mouse_px{-1.0f, -1.0f};
    bool mouse_moved = false;
    bool click = false;
};

struct MenuItem {
    enum class Kind : uint8_t { Button, Slider };

    std::string label;
    std::string blurb; // dim descriptive line shown while this item is selected
    Kind kind = Kind::Button;
    bool enabled = true;
    bool destructive = false; // red styling (ABANDON et al.)
    MenuAction action = MenuAction::None;
    int push_screen = -1; // >= 0: pushes MenuScreen(push_screen) instead of acting
    int payload = -1;     // SelectClass: the roster payload
    // Slider: bound console variable (skipped if the cvar doesn't exist).
    std::string cvar;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.05f;
    bool integer = false; // display without decimals
};

// One choice on the class-select screen, injected by the app from ContentDB
// (the menu itself stays content-blind).
struct RosterEntry {
    std::string label;
    std::string blurb;
    int payload = 0;
};

class MenuSystem {
public:
    // Resets the stack to a single root screen.
    void open(MenuScreen root);
    void close(); // gameplay: no menu active
    bool active() const { return !stack_.empty(); }
    MenuScreen current() const { return stack_.back(); }
    size_t depth() const { return stack_.size(); }

    // Extra line under the header (e.g. the death screen's final score).
    void set_status_line(std::string line) { status_line_ = std::move(line); }

    // Class-select roster + which payload is currently active.
    void set_class_roster(std::vector<RosterEntry> roster, int current_payload) {
        class_roster_ = std::move(roster);
        roster_current_ = current_payload;
    }
    int chosen_payload() const { return chosen_payload_; }

    // Rebuilds the current screen's items, applies navigation/mouse input,
    // adjusts bound cvars, and returns the action the app should execute.
    MenuAction update(const MenuInput& in, glm::vec2 viewport);

    // Draws the screen built by the last update().
    void render(FrameView& view, const FontAtlas& font) const;

    // Introspection (used by tests and the mouse hit-tests).
    const std::vector<MenuItem>& items() const { return items_; }
    size_t selection() const { return selection_; }
    // {x, y, w, h} of an item row for the viewport used in the last update().
    glm::vec4 item_rect(size_t index) const;

private:
    void rebuild_items();
    void select_first_enabled();
    void move_selection(int dir);
    MenuAction activate(size_t index);
    void adjust_slider(size_t index, int dir);

    std::vector<MenuScreen> stack_;
    std::vector<MenuItem> items_;
    size_t selection_ = 0;
    glm::vec2 viewport_{1280.0f, 720.0f};
    std::string status_line_;
    std::vector<RosterEntry> class_roster_;
    int roster_current_ = 0;
    int chosen_payload_ = -1;
};

} // namespace ds
