#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ds {

struct AppConfig {
    bool headless = false;
    uint64_t seed = 1;
    int64_t max_ticks = -1;              // >= 0: exit after N sim ticks (headless smoke tests)
    std::string bot;                     // headless input source: "", "walk", "walk_attack"
    std::filesystem::path telemetry_dir; // overrides the default pref-path telemetry location
    bool print_map = false;              // dump the generated floor as ASCII and exit
};

class App {
public:
    explicit App(AppConfig cfg);
    int run();

private:
    int run_headless();
    int run_windowed();

    AppConfig cfg_;
};

} // namespace ds
