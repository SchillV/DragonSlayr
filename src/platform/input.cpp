#include "platform/input.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <string>
#include <utility>

namespace ds {

namespace {

struct DefaultBinding {
    Action action;
    const char* json_key;
    SDL_Scancode scancode;
};

constexpr DefaultBinding kDefaults[] = {
    {Action::MoveForward, "move_forward", SDL_SCANCODE_W},
    {Action::MoveBack, "move_back", SDL_SCANCODE_S},
    {Action::StrafeLeft, "strafe_left", SDL_SCANCODE_A},
    {Action::StrafeRight, "strafe_right", SDL_SCANCODE_D},
    {Action::Walk, "walk", SDL_SCANCODE_LSHIFT},
    {Action::Dash, "dash", SDL_SCANCODE_SPACE},
    {Action::Interact, "interact", SDL_SCANCODE_E},
    {Action::ToggleConsole, "toggle_console", SDL_SCANCODE_GRAVE},
    {Action::ToggleDebugUi, "toggle_debug_ui", SDL_SCANCODE_F2},
    {Action::ToggleMouseCapture, "toggle_mouse_capture", SDL_SCANCODE_F1},
};

} // namespace

void InputMap::load_bindings(const std::filesystem::path& json_path) {
    for (const DefaultBinding& def : kDefaults) {
        bindings_[static_cast<size_t>(def.action)] = def.scancode;
    }

    std::ifstream f(json_path);
    if (!f) {
        log_info("no bindings file at {}, using defaults", json_path.string());
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        log_warn("bindings file {} is not valid JSON, using defaults", json_path.string());
        return;
    }
    for (const DefaultBinding& def : kDefaults) {
        const auto it = doc.find(def.json_key);
        if (it == doc.end() || !it->is_string()) {
            continue;
        }
        const std::string name = it->get<std::string>();
        const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
        if (code == SDL_SCANCODE_UNKNOWN) {
            log_warn("bindings: unknown key name '{}' for {}", name, def.json_key);
            continue;
        }
        bindings_[static_cast<size_t>(def.action)] = code;
    }
}

void InputMap::handle_event(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool is_down = ev.type == SDL_EVENT_KEY_DOWN;
        for (size_t i = 0; i < bindings_.size(); ++i) {
            if (bindings_[i] == ev.key.scancode) {
                if (is_down && !down_[i] && !ev.key.repeat) {
                    pressed_[i] = true;
                }
                down_[i] = is_down;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool is_down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (ev.button.button == SDL_BUTTON_LEFT) {
            attack_primary_down_ = is_down;
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            attack_secondary_down_ = is_down;
        }
        break;
    }
    default:
        break;
    }
}

bool InputMap::take_pressed(Action a) {
    return std::exchange(pressed_[static_cast<size_t>(a)], false);
}

PlayerCmd InputMap::make_cmd(float yaw, float pitch) {
    PlayerCmd cmd;
    cmd.yaw = yaw;
    cmd.pitch = pitch;

    const glm::vec2 fwd{std::cos(yaw), std::sin(yaw)};
    const glm::vec2 right{-std::sin(yaw), std::cos(yaw)};
    glm::vec2 wish{0.0f};
    if (down(Action::MoveForward)) wish += fwd;
    if (down(Action::MoveBack)) wish -= fwd;
    if (down(Action::StrafeRight)) wish += right;
    if (down(Action::StrafeLeft)) wish -= right;
    if (glm::dot(wish, wish) > 0.0f) {
        wish = glm::normalize(wish);
    }
    cmd.wish_dir = wish;

    cmd.run = !down(Action::Walk);
    cmd.dash = take_pressed(Action::Dash);
    cmd.interact = take_pressed(Action::Interact);
    cmd.attack_primary = attack_primary_down_;
    cmd.attack_secondary = attack_secondary_down_;
    return cmd;
}

} // namespace ds
