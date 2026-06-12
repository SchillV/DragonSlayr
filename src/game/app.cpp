#include "game/app.hpp"

#include "core/log.hpp"
#include "platform/platform.hpp"
#include "render/debug_ui.hpp"
#include "render/gpu_renderer.hpp"
#include "render/texture_load.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

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

Image fallback_texture() {
    // 2x2 magenta/black checker so a missing file is loud but not fatal.
    Image img;
    img.width = img.height = 2;
    img.pixels = {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};
    return img;
}

Image load_texture_or_fallback(const std::filesystem::path& path) {
    if (auto img = load_image(path)) {
        return std::move(*img);
    }
    return fallback_texture();
}

void add_quad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, float layer,
              float shade) {
    const auto base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, {0.0f, 0.0f}, layer, shade});
    m.vertices.push_back({b, {1.0f, 0.0f}, layer, shade});
    m.vertices.push_back({c, {1.0f, 1.0f}, layer, shade});
    m.vertices.push_back({d, {0.0f, 1.0f}, layer, shade});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// Temporary M1 scene: a floor, a short wall, proof of texture array + depth + fog.
MeshData make_test_diorama() {
    MeshData m;
    constexpr float kFloorLayer = 1.0f;
    constexpr float kWallLayer = 0.0f;
    for (int x = -4; x < 4; ++x) {
        for (int z = -4; z < 4; ++z) {
            const auto fx = static_cast<float>(x);
            const auto fz = static_cast<float>(z);
            add_quad(m, {fx, 0.0f, fz}, {fx + 1.0f, 0.0f, fz}, {fx + 1.0f, 0.0f, fz + 1.0f},
                     {fx, 0.0f, fz + 1.0f}, kFloorLayer, 0.85f);
        }
    }
    for (int i = -1; i <= 1; ++i) {
        const auto fx = static_cast<float>(i);
        add_quad(m, {fx, 1.0f, 0.0f}, {fx + 1.0f, 1.0f, 0.0f}, {fx + 1.0f, 0.0f, 0.0f},
                 {fx, 0.0f, 0.0f}, kWallLayer, 1.0f);
    }
    return m;
}

} // namespace

App::App(AppConfig cfg) : cfg_(std::move(cfg)) {}

int App::run() {
    if (cfg_.print_map) {
        log_warn("--print-map is not implemented yet (arrives with dungeon generation)");
        return 0;
    }
    Platform platform;
    if (!platform.init(cfg_.headless)) {
        return 1;
    }
    const int rc = cfg_.headless ? run_headless() : run_windowed(platform);
    platform.shutdown();
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

int App::run_windowed(Platform& platform) {
    SDL_Window* window = platform.create_window("DragonSlayr", 1280, 720);
    if (!window) {
        return 1;
    }

    const char* base_path = SDL_GetBasePath();
    const std::filesystem::path shader_dir =
        (base_path ? std::filesystem::path(base_path) : std::filesystem::path(".")) / "shaders";

    GpuRenderer renderer(shader_dir);
    if (!renderer.init(window)) {
        return 1;
    }

    DebugUi ui;
    if (ui.init(window, renderer.device(), renderer.swapchain_format())) {
        renderer.set_debug_ui(&ui);
    } else {
        log_warn("debug UI unavailable, continuing without it");
    }

    const std::filesystem::path tex_dir = find_asset_root() / "textures";
    const std::vector<Image> layers = {
        load_texture_or_fallback(tex_dir / "wall.ppm"),   // layer 0
        load_texture_or_fallback(tex_dir / "floor.ppm"),  // layer 1
        load_texture_or_fallback(tex_dir / "ceiling.ppm") // layer 2
    };
    renderer.set_world_textures(layers);
    renderer.set_dungeon_mesh(make_test_diorama());

    StubWorld world;
    bool running = true;
    auto prev = Clock::now();
    const auto start = prev;
    double acc = 0.0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            const bool ui_captured = ui.process_event(ev);
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ui_captured) {
                if (ev.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (ev.key.key == SDLK_F1) {
                    const bool captured = SDL_GetWindowRelativeMouseMode(window);
                    SDL_SetWindowRelativeMouseMode(window, !captured);
                } else if (ev.key.key == SDLK_F2) {
                    ui.visible = !ui.visible;
                }
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

        ui.add_frame_sample(static_cast<float>(frame_dt * 1000.0));
        ui.new_frame();
        ui.build();

        // Orbit camera around the diorama until the player controller exists (M3).
        const double t = seconds_since(start);
        FrameView view;
        view.time = t;
        const float angle = static_cast<float>(t * 0.4);
        view.camera.pos = {3.0f * std::cos(angle), 1.4f, 3.0f * std::sin(angle)};
        const glm::vec3 target{0.0f, 0.5f, 0.0f};
        const glm::vec3 dir = glm::normalize(target - view.camera.pos);
        view.camera.yaw = std::atan2(dir.z, dir.x);
        view.camera.pitch = std::asin(dir.y);
        view.camera.fov_deg = 75.0f;
        renderer.render(view);

        if (cfg_.max_ticks >= 0 && static_cast<int64_t>(world.tick_count) >= cfg_.max_ticks) {
            running = false;
        }
    }

    ui.shutdown();
    renderer.shutdown();
    return 0;
}

} // namespace ds
