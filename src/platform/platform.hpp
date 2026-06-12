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

// Walks up from the executable looking for an assets/ directory: dev builds run
// from build/<preset>/, shipped builds keep assets next to the binary.
std::filesystem::path find_asset_root();

} // namespace ds
