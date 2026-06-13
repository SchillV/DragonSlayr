#pragma once

#include <filesystem>

struct SDL_Window;

namespace ds {

class Platform {
public:
    bool init(bool headless); // selects the dummy video driver when headless
    void shutdown();

    SDL_Window* create_window(const char* title, int w, int h);
    SDL_Window* window() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    bool initialized_ = false;
};

// Walks up from the executable looking for a named resource directory (e.g.
// "assets" or "shaders"). Handles both single-config layouts (Ninja: exe and
// shaders under build/<preset>/) and multi-config layouts (Visual Studio: exe
// in build/<cfg>/, shaders staged beside it post-build), plus shipped builds
// where the directory sits right next to the binary.
std::filesystem::path find_resource_dir(const char* name);

// Convenience wrapper for the assets/ directory.
std::filesystem::path find_asset_root();

} // namespace ds
