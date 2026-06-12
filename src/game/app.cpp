#include "game/app.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"
#include "render/debug_ui.hpp"
#include "render/dungeon_mesh.hpp"
#include "render/gpu_renderer.hpp"
#include "render/texture_load.hpp"
#include "sim/bot.hpp"
#include "sim/components.hpp"
#include "sim/dungeon_gen.hpp"
#include "sim/world.hpp"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace ds {

namespace {

constexpr double kTickDt = 1.0 / 60.0;
constexpr float kMaxPitch = glm::radians(89.0f);

CVar& r_fov = cvar_register("r.fov", 75.0f, "vertical field of view, degrees (66-110)");
CVar& r_eye_height = cvar_register("r.eye_height", 0.55f, "camera height above the floor, tiles");
CVar& in_sensitivity = cvar_register("in.sensitivity", 2.2f, "mouselook, radians per 1000 px");
CVar& fs_hot_reload = cvar_register("fs.hot_reload", 1.0f, "poll data JSONs for changes");

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

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

// data/player.json maps cvar names to values — the owner's tuning surface.
void load_cvar_overrides(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) {
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        log_warn("cvar override file {} is not valid JSON", path.string());
        return;
    }
    for (const auto& [key, value] : doc.items()) {
        if (!value.is_number()) {
            continue;
        }
        if (CVar* cvar = cvar_find(key)) {
            cvar->value = value.get<float>(); // overrides are tuning, not cheating
        } else {
            log_warn("cvar override for unknown cvar '{}'", key);
        }
    }
}

// Sprite-name -> texture-array-layer mapping, rebuilt whenever content changes.
struct SpriteAtlas {
    std::vector<std::string> names;

    int layer_of(std::string_view name) const {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

SpriteAtlas rebuild_sprite_atlas(IRenderer& renderer, const ContentDB& content,
                                 const std::filesystem::path& tex_dir) {
    SpriteAtlas atlas;
    std::vector<Image> layers;
    for (const EnemyDef& def : content.enemies) {
        if (atlas.layer_of(def.sprite) >= 0) {
            continue;
        }
        atlas.names.push_back(def.sprite);
        const std::filesystem::path png = tex_dir / (def.sprite + ".png");
        const std::filesystem::path ppm = tex_dir / (def.sprite + ".ppm");
        std::error_code ec;
        auto img = load_image(std::filesystem::exists(png, ec) ? png : ppm, /*black_to_alpha=*/true);
        layers.push_back(img ? std::move(*img) : fallback_texture());
    }
    renderer.set_sprite_textures(layers);
    return atlas;
}

void load_content(World& world, const std::filesystem::path& data_dir) {
    std::string error;
    if (!world.content.load_enemies(data_dir / "enemies.json", &error)) {
        log_warn("enemy content unavailable: {}", error);
    } else {
        log_info("loaded {} enemy defs", world.content.enemies.size());
    }
}

bool player_state_valid(const World& world) {
    const auto& tr = world.reg.get<Transform>(world.player);
    if (!std::isfinite(tr.pos.x) || !std::isfinite(tr.pos.y)) {
        log_error("player position not finite at tick {}", world.tick_count);
        return false;
    }
    const int tx = static_cast<int>(std::floor(tr.pos.x));
    const int ty = static_cast<int>(std::floor(tr.pos.y));
    if (!world.map().tiles.in_bounds(tx, ty)) {
        log_error("player out of bounds at tick {} ({:.2f}, {:.2f})", world.tick_count, tr.pos.x,
                  tr.pos.y);
        return false;
    }
    if (world.map().solid(tx, ty)) {
        log_error("player inside solid tile at tick {} ({:.2f}, {:.2f})", world.tick_count,
                  tr.pos.x, tr.pos.y);
        return false;
    }
    return true;
}

bool sim_state_valid(const World& world) {
    if (!player_state_valid(world)) {
        return false;
    }
    for (auto [e, enemy, tr] : world.reg.view<const Enemy, const Transform>().each()) {
        if (!std::isfinite(tr.pos.x) || !std::isfinite(tr.pos.y)) {
            log_error("enemy position not finite at tick {}", world.tick_count);
            return false;
        }
        if (world.map().solid_at(tr.pos)) {
            log_error("enemy inside solid tile at tick {} ({:.2f}, {:.2f})", world.tick_count,
                      tr.pos.x, tr.pos.y);
            return false;
        }
    }
    return true;
}

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
    load_cvar_overrides(find_asset_root() / "data" / "player.json");
    const int rc = cfg_.headless ? run_headless() : run_windowed(platform);
    platform.shutdown();
    return rc;
}

int App::run_headless() {
    GenParams params;
    params.seed = cfg_.seed;
    DungeonResult dungeon = generate_dungeon(params);
    if (dungeon.rooms.empty()) {
        log_error("generation produced no rooms (seed {})", cfg_.seed);
        return 1;
    }
    World world;
    load_content(world, find_asset_root() / "data");
    world.init_from_dungeon(std::move(dungeon), cfg_.seed);
    Bot bot(cfg_.bot);

    const int64_t target = cfg_.max_ticks >= 0 ? cfg_.max_ticks : 600;
    log_info("headless run: seed={} ticks={} bot='{}'", cfg_.seed, target, cfg_.bot);

    const auto t0 = Clock::now();
    while (static_cast<int64_t>(world.tick_count) < target) {
        const PlayerCmd cmd = bot.active() ? bot.make_cmd(world) : PlayerCmd{};
        world.tick(cmd, static_cast<float>(kTickDt));
        if (world.tick_count % 60 == 0 && !sim_state_valid(world)) {
            return 1;
        }
    }
    if (!sim_state_valid(world)) {
        return 1;
    }
    const double elapsed = std::max(seconds_since(t0), 1e-9);
    const auto& tr = world.reg.get<Transform>(world.player);
    int enemies_alive = 0;
    int enemies_chasing = 0;
    for (auto [e, enemy] : world.reg.view<const Enemy>().each()) {
        ++enemies_alive;
        if (enemy.state != AiState::Idle) {
            ++enemies_chasing;
        }
    }
    log_info("{} ticks, sim rate: {:.0f} tps, player at ({:.1f}, {:.1f}), enemies {} ({} active)",
             world.tick_count, static_cast<double>(world.tick_count) / elapsed, tr.pos.x, tr.pos.y,
             enemies_alive, enemies_chasing);
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

    const std::filesystem::path asset_root = find_asset_root();
    const std::vector<Image> layers = {
        load_texture_or_fallback(asset_root / "textures" / "wall.ppm"),   // kLayerWall
        load_texture_or_fallback(asset_root / "textures" / "floor.ppm"),  // kLayerFloor
        load_texture_or_fallback(asset_root / "textures" / "ceiling.ppm") // kLayerCeiling
    };
    renderer.set_world_textures(layers);

    InputMap input;
    input.load_bindings(asset_root / "data" / "bindings.json");

    World world;
    load_content(world, asset_root / "data");
    SpriteAtlas sprite_atlas =
        rebuild_sprite_atlas(renderer, world.content, asset_root / "textures");

    const std::filesystem::path enemies_path = asset_root / "data" / "enemies.json";
    std::error_code mtime_ec;
    auto enemies_mtime = std::filesystem::last_write_time(enemies_path, mtime_ec);
    double reload_poll_timer = 0.0;

    uint64_t seed = cfg_.seed;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;
    auto regenerate = [&](uint64_t new_seed) {
        seed = new_seed;
        GenParams params;
        params.seed = seed;
        world.init_from_dungeon(generate_dungeon(params), seed);
        renderer.set_dungeon_mesh(build_dungeon_mesh(world.map()));
        cam_yaw = 0.0f;
        cam_pitch = 0.0f;
        log_info("dungeon generated: seed={} rooms={} enemy spawns={}", seed,
                 world.dungeon.rooms.size(), world.dungeon.enemy_spawns.size());
    };
    regenerate(seed);

    bool running = true;
    con_register("quit", "exit the game", [&](auto, std::string& fb) {
        running = false;
        fb = "bye";
    });
    con_register("regen", "regenerate the dungeon: regen [seed]",
                 [&](std::span<const std::string_view> args, std::string& fb) {
                     uint64_t s = seed + 1;
                     if (!args.empty()) {
                         std::from_chars(args[0].data(), args[0].data() + args[0].size(), s);
                     }
                     regenerate(s);
                     fb = std::format("regenerated with seed {}", s);
                 });
    con_register("noclip", "toggle wall collision for the player", [&](auto, std::string& fb) {
        CVar* cv = cvar_find("sv.noclip");
        cvar_set(*cv, cv->as_bool() ? 0.0f : 1.0f);
        fb = std::format("noclip {}", cv->as_bool() ? "ON [CHEAT]" : "off");
    });

    SDL_SetWindowRelativeMouseMode(window, true);

    auto prev = Clock::now();
    double acc = 0.0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            const bool ui_captured = ui.process_event(ev);
            // Key releases always reach the input map, otherwise a key held
            // while the console opens would stick forever.
            if (ev.type == SDL_EVENT_KEY_UP || !ui_captured) {
                input.handle_event(ev);
            }
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
                const float sens = in_sensitivity.value / 1000.0f;
                cam_yaw += ev.motion.xrel * sens;
                cam_pitch = std::clamp(cam_pitch - ev.motion.yrel * sens, -kMaxPitch, kMaxPitch);
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ui_captured && ev.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        if (input.take_pressed(Action::ToggleMouseCapture)) {
            SDL_SetWindowRelativeMouseMode(window, !SDL_GetWindowRelativeMouseMode(window));
        }
        if (input.take_pressed(Action::ToggleDebugUi)) {
            ui.visible = !ui.visible;
        }

        const auto now = Clock::now();
        double frame_dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        frame_dt = std::min(frame_dt, 0.25); // spiral-of-death clamp

        // Hot reload: poll data file mtimes a couple of times a second.
        reload_poll_timer += frame_dt;
        if (fs_hot_reload.as_bool() && reload_poll_timer >= 0.5) {
            reload_poll_timer = 0.0;
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(enemies_path, ec);
            if (!ec && mtime != enemies_mtime) {
                enemies_mtime = mtime;
                ContentDB fresh;
                std::string error;
                if (fresh.load_enemies(enemies_path, &error)) {
                    world.apply_content(std::move(fresh));
                    sprite_atlas =
                        rebuild_sprite_atlas(renderer, world.content, asset_root / "textures");
                } else {
                    log_error("hot reload rejected: {}", error);
                }
            }
        }

        acc += frame_dt;
        while (acc >= kTickDt) {
            const PlayerCmd cmd = input.make_cmd(cam_yaw, cam_pitch);
            world.tick(cmd, static_cast<float>(kTickDt));
            acc -= kTickDt;
        }

        // Interpolated camera: position between the last two ticks, angles at
        // render rate (zero perceived mouse latency).
        const float alpha = static_cast<float>(acc / kTickDt);
        const auto& tr = world.reg.get<Transform>(world.player);
        const auto& prev_tr = world.reg.get<PrevTransform>(world.player);
        const glm::vec2 pos = glm::mix(prev_tr.pos, tr.pos, alpha);
        const float speed = glm::length(world.reg.get<Velocity>(world.player).v);

        ui.add_frame_sample(static_cast<float>(frame_dt * 1000.0));
        ui.new_frame();
        DebugUiState ui_state;
        ui_state.map = &world.map();
        ui_state.world = &world;
        ui_state.cam_pos = pos;
        ui_state.cam_yaw = cam_yaw;
        ui_state.player_speed = speed;
        ui_state.seed = seed;
        ui_state.regenerate = regenerate;
        ui.build(ui_state);

        FrameView view;
        view.camera.pos = {pos.x, r_eye_height.value, pos.y};
        view.camera.yaw = cam_yaw;
        view.camera.pitch = cam_pitch;
        view.camera.fov_deg = std::clamp(r_fov.value, 66.0f, 110.0f);
        for (auto [e, enemy, etr, eprev] :
             world.reg.view<const Enemy, const Transform, const PrevTransform>().each()) {
            const EnemyDef& def = world.content.enemies[enemy.def];
            const int layer = sprite_atlas.layer_of(def.sprite);
            if (layer < 0) {
                continue;
            }
            const glm::vec2 ep = glm::mix(eprev.pos, etr.pos, alpha);
            view.sprites.push_back(
                {{ep.x, 0.0f, ep.y}, def.sprite_size, static_cast<float>(layer), 0.0f});
        }
        renderer.render(view);

        if (cfg_.max_ticks >= 0 && static_cast<int64_t>(world.tick_count) >= cfg_.max_ticks) {
            running = false;
        }
    }

    con_unregister_all();
    ui.shutdown();
    renderer.shutdown();
    return 0;
}

} // namespace ds
