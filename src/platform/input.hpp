#pragma once

#include "sim/player.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>

namespace ds {

enum class Action : uint8_t {
    MoveForward,
    MoveBack,
    StrafeLeft,
    StrafeRight,
    Walk, // hold to slow down — running is the default
    Dash,
    Interact,
    ToggleConsole,
    ToggleDebugUi,
    ToggleMouseCapture,
    Count,
};

// Keyboard scancode bindings (rebindable via assets/data/bindings.json) plus
// hardcoded mouse buttons for attacks. Edges latch until consumed so a fast
// frame can't swallow a keypress before the next sim tick runs.
class InputMap {
public:
    void load_bindings(const std::filesystem::path& json_path); // missing file = defaults

    void handle_event(const SDL_Event& ev);

    bool down(Action a) const { return down_[static_cast<size_t>(a)]; }
    bool take_pressed(Action a); // consume the latched edge

    // Builds the world-space command for one sim tick; consumes edges.
    PlayerCmd make_cmd(float yaw, float pitch);

private:
    std::array<SDL_Scancode, static_cast<size_t>(Action::Count)> bindings_{};
    std::array<bool, static_cast<size_t>(Action::Count)> down_{};
    std::array<bool, static_cast<size_t>(Action::Count)> pressed_{};
    bool attack_primary_down_ = false;
    bool attack_secondary_down_ = false;
};

} // namespace ds
