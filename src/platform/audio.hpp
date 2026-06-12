#pragma once

#include <filesystem>
#include <string_view>

namespace ds {

// miniaudio-backed effect player. Every effect ships as a code-synthesized
// blip (no binary assets in the repo); dropping assets/sounds/<name>.wav next
// to the game overrides it. Not initialized at all in headless runs.
class Audio {
public:
    bool init(const std::filesystem::path& sounds_dir);
    void shutdown();
    ~Audio();

    // Known names: swing, bolt_fire, hit, hurt, kill, dash. Unknown = no-op.
    void play(std::string_view name);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ds
