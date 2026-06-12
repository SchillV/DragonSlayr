#include "game/app.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace ds {

namespace {

constexpr double kTickDt = 1.0 / 60.0;

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Stand-in until the real sim arrives (M2/M3).
struct StubWorld {
    uint64_t tick_count = 0;

    void tick(float) { ++tick_count; }
};

} // namespace

App::App(AppConfig cfg) : cfg_(std::move(cfg)) {}

int App::run() {
    if (cfg_.print_map) {
        log_warn("--print-map is not implemented yet (arrives with dungeon generation)");
        return 0;
    }
    if (cfg_.headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log_error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    log_info("SDL initialized (video driver: {})", SDL_GetCurrentVideoDriver());

    const int rc = cfg_.headless ? run_headless() : run_windowed();
    SDL_Quit();
    return rc;
}

int App::run_headless() {
    // No window, no renderer, no pacing: run the sim flat-out for N ticks.
    const int64_t target = cfg_.max_ticks >= 0 ? cfg_.max_ticks : 600;
    log_info("headless run: seed={} ticks={} bot='{}'", cfg_.seed, target, cfg_.bot);

    StubWorld world;
    const auto t0 = Clock::now();
    while (static_cast<int64_t>(world.tick_count) < target) {
        world.tick(static_cast<float>(kTickDt));
    }
    const double elapsed = std::max(seconds_since(t0), 1e-9);
    log_info("{} ticks, sim rate: {:.0f} tps", world.tick_count, world.tick_count / elapsed);
    return 0;
}

int App::run_windowed() {
    SDL_Window* window = SDL_CreateWindow("DragonSlayr", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        log_error("SDL_CreateWindow failed: {}", SDL_GetError());
        return 1;
    }

    StubWorld world;
    bool running = true;
    auto prev = Clock::now();
    double acc = 0.0;
    double title_timer = 0.0;
    double frame_ms_avg = 0.0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        const auto now = Clock::now();
        double frame_dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        frame_dt = std::min(frame_dt, 0.25); // spiral-of-death clamp

        acc += frame_dt;
        while (acc >= kTickDt) {
            world.tick(static_cast<float>(kTickDt));
            acc -= kTickDt;
        }

        // Rendering arrives in M1; meanwhile show frame time in the title bar.
        frame_ms_avg = frame_ms_avg * 0.95 + frame_dt * 1000.0 * 0.05;
        title_timer += frame_dt;
        if (title_timer >= 0.25) {
            title_timer = 0.0;
            const std::string title = std::format("DragonSlayr — {:.2f} ms", frame_ms_avg);
            SDL_SetWindowTitle(window, title.c_str());
        }
        SDL_Delay(1); // don't spin a core; the swapchain takes over pacing in M1

        if (cfg_.max_ticks >= 0 && static_cast<int64_t>(world.tick_count) >= cfg_.max_ticks) {
            running = false;
        }
    }

    SDL_DestroyWindow(window);
    return 0;
}

} // namespace ds
