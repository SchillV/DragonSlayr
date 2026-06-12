#include "game/app.hpp"

#include "core/log.hpp"
#include "platform/platform.hpp"
#include "render/debug_ui.hpp"
#include "render/dungeon_mesh.hpp"
#include "render/gpu_renderer.hpp"
#include "render/texture_load.hpp"
#include "sim/dungeon_gen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace ds {

namespace {

constexpr double kTickDt = 1.0 / 60.0;
constexpr float kMouseSens = 0.0022f; // radians per pixel
constexpr float kMaxPitch = glm::radians(89.0f);

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Stand-in until the real sim arrives (M3).
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

struct FlyCam {
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;

    void reset_to(glm::ivec2 spawn_tile) {
        pos = {static_cast<float>(spawn_tile.x) + 0.5f, 0.6f, static_cast<float>(spawn_tile.y) + 0.5f};
        yaw = 0.0f;
        pitch = 0.0f;
    }

    glm::vec3 forward() const {
        return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
    }
};

} // namespace

App::App(AppConfig cfg) : cfg_(std::move(cfg)) {}

int App::run() {
    if (cfg_.print_map) {
        GenParams params;
        params.seed = cfg_.seed;
        const DungeonResult dungeon = generate_dungeon(params);
        std::fputs(render_ascii(dungeon).c_str(), stdout);
        return dungeon.rooms.empty() ? 1 : 0;
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
        load_texture_or_fallback(tex_dir / "wall.ppm"),   // kLayerWall
        load_texture_or_fallback(tex_dir / "floor.ppm"),  // kLayerFloor
        load_texture_or_fallback(tex_dir / "ceiling.ppm") // kLayerCeiling
    };
    renderer.set_world_textures(layers);

    uint64_t seed = cfg_.seed;
    DungeonResult dungeon;
    FlyCam cam;
    auto regenerate = [&](uint64_t new_seed) {
        seed = new_seed;
        GenParams params;
        params.seed = seed;
        dungeon = generate_dungeon(params);
        renderer.set_dungeon_mesh(build_dungeon_mesh(dungeon.map));
        cam.reset_to(dungeon.player_spawn);
        log_info("dungeon generated: seed={} rooms={} spawns={}", seed, dungeon.rooms.size(),
                 dungeon.enemy_spawns.size());
    };
    regenerate(seed);

    SDL_SetWindowRelativeMouseMode(window, true);

    StubWorld world;
    bool running = true;
    auto prev = Clock::now();
    double acc = 0.0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            const bool ui_captured = ui.process_event(ev);
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
                cam.yaw += ev.motion.xrel * kMouseSens;
                cam.pitch = std::clamp(cam.pitch - ev.motion.yrel * kMouseSens, -kMaxPitch, kMaxPitch);
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

        // Noclip fly camera (render-rate; the real player controller lands in M3).
        if (!ui.wants_keyboard()) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float fly_speed = keys[SDL_SCANCODE_LSHIFT] ? 20.0f : 8.0f;
            const glm::vec3 fwd = cam.forward();
            const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 wish{0.0f};
            if (keys[SDL_SCANCODE_W]) wish += fwd;
            if (keys[SDL_SCANCODE_S]) wish -= fwd;
            if (keys[SDL_SCANCODE_D]) wish += right;
            if (keys[SDL_SCANCODE_A]) wish -= right;
            if (keys[SDL_SCANCODE_SPACE]) wish.y += 1.0f;
            if (keys[SDL_SCANCODE_LCTRL]) wish.y -= 1.0f;
            if (glm::dot(wish, wish) > 0.0f) {
                cam.pos += glm::normalize(wish) * fly_speed * static_cast<float>(frame_dt);
            }
        }

        ui.add_frame_sample(static_cast<float>(frame_dt * 1000.0));
        ui.new_frame();
        DebugUiState ui_state;
        ui_state.map = &dungeon.map;
        ui_state.cam_pos = {cam.pos.x, cam.pos.z};
        ui_state.cam_yaw = cam.yaw;
        ui_state.seed = seed;
        ui_state.regenerate = regenerate;
        ui.build(ui_state);

        FrameView view;
        view.camera.pos = cam.pos;
        view.camera.yaw = cam.yaw;
        view.camera.pitch = cam.pitch;
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
