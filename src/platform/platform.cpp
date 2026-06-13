#include "platform/platform.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

namespace ds {

bool Platform::init(bool headless) {
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log_error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    initialized_ = true;
    log_info("SDL initialized (video driver: {})", SDL_GetCurrentVideoDriver());
    return true;
}

void Platform::shutdown() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (initialized_) {
        SDL_Quit();
        initialized_ = false;
    }
}

SDL_Window* Platform::create_window(const char* title, int w, int h) {
    window_ = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        log_error("SDL_CreateWindow failed: {}", SDL_GetError());
    }
    return window_;
}

std::filesystem::path find_resource_dir(const char* name) {
    const char* base = SDL_GetBasePath();
    std::error_code ec;
    std::filesystem::path dir = base ? std::filesystem::path(base) : std::filesystem::current_path(ec);
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(dir / name, ec)) {
            return dir / name;
        }
        const auto parent = dir.parent_path();
        if (parent.empty() || parent == dir) {
            break;
        }
        dir = parent;
    }
    log_warn("resource dir '{}' not found near executable; falling back to ./{}", name, name);
    return std::filesystem::path(name);
}

std::filesystem::path find_asset_root() {
    return find_resource_dir("assets");
}

} // namespace ds
