#include "game/app.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "game/hud.hpp"
#include "game/menu.hpp"
#include "platform/audio.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"
#include "render/debug_ui.hpp"
#include "render/dungeon_mesh.hpp"
#include "render/font.hpp"
#include "render/gpu_renderer.hpp"
#include "render/texture_load.hpp"
#include "sim/items.hpp"
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
CVar& fx_shake = cvar_register("fx.shake", 1.0f, "camera kick scale (0 disables)");
CVar& fx_indicator_ttl = cvar_register("fx.indicator_ttl", 1.0f, "damage direction indicator lifetime, s");
CVar& fx_lowhp = cvar_register("fx.lowhp_threshold", 0.3f, "low-health warning threshold, fraction of max hp");
CVar& fx_hitmarker = cvar_register("fx.hitmarker", 1.0f, "hitmarker flashes (0 disables)");
CVar& snd_volume = cvar_register("snd.volume", 0.8f, "master volume, 0-1");

// Which mode the windowed session is in; the sim only ticks while Playing.
enum class GamePhase : uint8_t { Title, Playing, Paused, Dead };

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

// Soft radial glow for projectiles with no texture file (e.g. the bolt).
Image procedural_glow() {
    Image img;
    img.width = img.height = 16;
    img.pixels.resize(16 * 16 * 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const float dx = static_cast<float>(x) - 7.5f;
            const float dy = static_cast<float>(y) - 7.5f;
            const float a = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy) / 7.0f, 0.0f, 1.0f);
            const size_t i = (static_cast<size_t>(y) * 16 + x) * 4;
            img.pixels[i + 0] = static_cast<uint8_t>(170 + 85 * a);
            img.pixels[i + 1] = static_cast<uint8_t>(210 + 45 * a);
            img.pixels[i + 2] = 255;
            img.pixels[i + 3] = static_cast<uint8_t>(255.0f * a);
        }
    }
    return img;
}

Image load_sprite_image(const std::filesystem::path& tex_dir, const std::string& name) {
    const std::filesystem::path png = tex_dir / (name + ".png");
    const std::filesystem::path ppm = tex_dir / (name + ".ppm");
    std::error_code ec;
    if (std::filesystem::exists(png, ec) || std::filesystem::exists(ppm, ec)) {
        if (auto img = load_image(std::filesystem::exists(png, ec) ? png : ppm,
                                  /*black_to_alpha=*/true)) {
            return std::move(*img);
        }
    }
    if (name == "bolt") {
        return procedural_glow();
    }
    return fallback_texture();
}

SpriteAtlas rebuild_sprite_atlas(IRenderer& renderer, const ContentDB& content,
                                 const std::filesystem::path& tex_dir) {
    SpriteAtlas atlas;
    std::vector<Image> layers;
    auto add = [&](const std::string& name) {
        if (name.empty() || atlas.layer_of(name) >= 0) {
            return;
        }
        atlas.names.push_back(name);
        layers.push_back(scale_nearest(load_sprite_image(tex_dir, name), 64, 64));
    };
    for (const EnemyDef& def : content.enemies) {
        add(def.sprite);
    }
    for (const WeaponDef& def : content.weapons) {
        add(def.sprite);
    }
    for (const ItemDef& def : content.items) {
        add(def.sprite);
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
    error.clear();
    if (!world.content.load_weapons(data_dir / "weapons.json", &error)) {
        log_warn("weapon content unavailable: {}", error);
    } else {
        log_info("loaded {} weapon defs", world.content.weapons.size());
    }
    error.clear();
    if (!world.content.load_items(data_dir / "items.json", &error)) {
        log_warn("item content unavailable: {}", error);
    } else {
        log_info("loaded {} item defs", world.content.items.size());
    }
}

// First assets/fonts/*.ttf baked via stb_truetype, else the builtin 8x8 font.
FontAtlas load_game_font(const std::filesystem::path& asset_root) {
    std::error_code ec;
    const std::filesystem::path dir = asset_root / "fonts";
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".ttf") {
                continue;
            }
            std::ifstream f(entry.path(), std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f),
                                             std::istreambuf_iterator<char>{});
            FontAtlas atlas = bake_ttf_font(bytes, 24.0f);
            if (atlas.valid()) {
                log_info("font: baked {}", entry.path().filename().string());
                return atlas;
            }
        }
    }
    log_info("using builtin pixel font (drop a .ttf into assets/fonts/ to override)");
    return bake_builtin_font();
}

// 1x1 white + viewmodel textures, in the kOverlay* layer order the HUD assumes.
void load_overlay_textures(IRenderer& renderer, const std::filesystem::path& tex_dir) {
    // The viewmodel layers must match the white layer's dimensions (texture
    // array constraint), so white is 64x64 like the legacy art.
    Image white;
    white.width = white.height = 64;
    white.pixels.assign(64 * 64 * 4, 255);
    std::vector<Image> layers;
    layers.push_back(std::move(white));
    for (const char* name : {"sword", "hand"}) {
        auto img = load_image(tex_dir / (std::string(name) + ".ppm"), /*black_to_alpha=*/true);
        layers.push_back(scale_nearest(img ? std::move(*img) : fallback_texture(), 64, 64));
    }
    renderer.set_overlay_textures(layers);
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

std::filesystem::path telemetry_dir(const AppConfig& cfg) {
    if (!cfg.telemetry_dir.empty()) {
        return cfg.telemetry_dir;
    }
    if (char* pref = SDL_GetPrefPath("schillv", "DragonSlayr")) {
        std::filesystem::path dir = std::filesystem::path(pref) / "telemetry";
        SDL_free(pref);
        return dir;
    }
    return std::filesystem::path("telemetry");
}

// Headless smoke self-check: the written telemetry must parse and contain the
// events the run was supposed to produce.
bool validate_telemetry_file(const std::filesystem::path& path, bool expect_attacks) {
    std::ifstream f(path);
    if (!f) {
        log_error("telemetry file missing: {}", path.string());
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        log_error("telemetry file is not valid JSON: {}", path.string());
        return false;
    }
    if (!doc.contains("version") || !doc.contains("events") || !doc["events"].is_array()) {
        log_error("telemetry file missing version/events: {}", path.string());
        return false;
    }
    bool has_run_start = false;
    int attacks = 0;
    for (const auto& ev : doc["events"]) {
        const std::string type = ev.value("type", "");
        has_run_start |= type == "run_start";
        attacks += type == "player_attack" ? 1 : 0;
    }
    if (!has_run_start) {
        log_error("telemetry has no run_start event");
        return false;
    }
    if (expect_attacks && attacks == 0) {
        log_error("telemetry has no player_attack events");
        return false;
    }
    log_info("telemetry validated: {} events, {} attacks", doc["events"].size(), attacks);
    return true;
}

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
    log_info("{} ticks, sim rate: {:.0f} tps, player at ({:.1f}, {:.1f}), enemies {} ({} active), "
             "score {}",
             world.tick_count, static_cast<double>(world.tick_count) / elapsed, tr.pos.x, tr.pos.y,
             enemies_alive, enemies_chasing, world.score);

    const std::filesystem::path written = world.telem.write_json(
        telemetry_dir(cfg_), world.content, world.player_dead ? "death" : "complete",
        cvar_any_cheat_touched());
    if (written.empty()) {
        return 1;
    }
    if (!validate_telemetry_file(written, cfg_.bot == "walk_attack")) {
        return 1;
    }
    return 0;
}

int App::run_windowed(Platform& platform) {
    SDL_Window* window = platform.create_window("DragonSlayr", 1280, 720);
    if (!window) {
        return 1;
    }

    const std::filesystem::path shader_dir = find_resource_dir("shaders");

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
    load_overlay_textures(renderer, asset_root / "textures");

    const FontAtlas font = load_game_font(asset_root);
    renderer.set_font_texture(font.image);

    Audio audio;
    audio.init(asset_root / "sounds");

    const std::filesystem::path enemies_path = asset_root / "data" / "enemies.json";
    const std::filesystem::path items_path = asset_root / "data" / "items.json";
    std::error_code mtime_ec;
    auto enemies_mtime = std::filesystem::last_write_time(enemies_path, mtime_ec);
    auto items_mtime = std::filesystem::last_write_time(items_path, mtime_ec);
    double reload_poll_timer = 0.0;

    uint64_t seed = cfg_.seed;
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;
    uint64_t telem_cursor = 0;
    std::vector<TelemetryEvent> fresh_events;
    bool was_dead = false;
    bool run_recorded = false; // telemetry written for the current run

    // Combat-feedback state: render-side only, fed by the telemetry stream.
    std::vector<DamageIndicator> fx_indicators;
    float fx_hitmarker_t = 0.0f;
    bool fx_hitmarker_kill = false;
    glm::vec2 fx_kick{0.0f};    // yaw/pitch camera impulse, decays exponentially
    float fx_chip_hp = -1.0f;   // trailing health-bar chip value
    float fx_chip_delay = 0.0f; // pause before the chip starts draining
    double fx_heartbeat_timer = 0.0;
    double run_time = 0.0; // drives HUD pulsing

    auto write_telemetry = [&](std::string_view outcome) {
        if (run_recorded || world.tick_count < 60) {
            return; // nothing meaningful happened
        }
        run_recorded = true;
        world.telem.write_json(telemetry_dir(cfg_), world.content, outcome,
                               cvar_any_cheat_touched());
    };
    auto regenerate = [&](uint64_t new_seed) {
        write_telemetry(world.player_dead ? "death" : "restart");
        seed = new_seed;
        GenParams params;
        params.seed = seed;
        world.init_from_dungeon(generate_dungeon(params), seed);
        renderer.set_dungeon_mesh(build_dungeon_mesh(world.map()));
        cam_yaw = 0.0f;
        cam_pitch = 0.0f;
        telem_cursor = 0;
        was_dead = false;
        run_recorded = false;
        fx_indicators.clear();
        fx_hitmarker_t = 0.0f;
        fx_kick = {0.0f, 0.0f};
        fx_chip_hp = world.reg.get<Health>(world.player).hp;
        fx_chip_delay = 0.0f;
        fx_heartbeat_timer = 0.0;
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
    con_register("give", "grant an item by id: give <item_id>",
                 [&](std::span<const std::string_view> args, std::string& fb) {
                     if (args.empty()) {
                         fb = "usage: give <item_id>";
                         return;
                     }
                     const int idx = world.content.find_item(args[0]);
                     if (idx < 0) {
                         fb = std::format("unknown item '{}'", args[0]);
                         return;
                     }
                     grant_item(world, idx);
                     fb = std::format("granted {}", world.content.items[static_cast<size_t>(idx)].name);
                 });

    // Menu / phase state. The game boots onto the Title screen over a live
    // dungeon backdrop; the sim only ticks while Playing.
    GamePhase phase = GamePhase::Title;
    MenuSystem menu;
    float applied_volume = -1.0f;
    glm::vec2 last_mouse_px{-1.0f, -1.0f};
    auto enter_playing = [&] {
        menu.close();
        phase = GamePhase::Playing;
        SDL_SetWindowRelativeMouseMode(window, true);
    };
    auto open_menu = [&](MenuScreen s, GamePhase p) {
        menu.open(s);
        phase = p;
        SDL_SetWindowRelativeMouseMode(window, false);
    };
    open_menu(MenuScreen::Title, GamePhase::Title);

    auto prev = Clock::now();
    double acc = 0.0;

    while (running) {
        MenuInput menu_input;
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
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ui_captured) {
                const SDL_Keycode k = ev.key.key;
                if (menu.active()) {
                    menu_input.up |= k == SDLK_UP || k == SDLK_W;
                    menu_input.down |= k == SDLK_DOWN || k == SDLK_S;
                    menu_input.left |= k == SDLK_LEFT || k == SDLK_A;
                    menu_input.right |= k == SDLK_RIGHT || k == SDLK_D;
                    menu_input.select |= k == SDLK_RETURN || k == SDLK_SPACE;
                    menu_input.back |= k == SDLK_ESCAPE;
                } else if (k == SDLK_ESCAPE && phase == GamePhase::Playing) {
                    open_menu(MenuScreen::Pause, GamePhase::Paused);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       ev.button.button == SDL_BUTTON_LEFT && menu.active() && !ui_captured) {
                menu_input.click = true;
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

        // Hot reload: poll data file mtimes a couple of times a second. Each
        // reload starts from a copy of the live db so the categories the
        // changed file doesn't cover survive intact.
        reload_poll_timer += frame_dt;
        if (fs_hot_reload.as_bool() && reload_poll_timer >= 0.5) {
            reload_poll_timer = 0.0;
            auto reload_file = [&](const std::filesystem::path& path,
                                   bool (ContentDB::*load)(const std::filesystem::path&,
                                                           std::string*)) {
                ContentDB fresh = world.content;
                std::string error;
                if ((fresh.*load)(path, &error)) {
                    world.apply_content(std::move(fresh));
                    sprite_atlas =
                        rebuild_sprite_atlas(renderer, world.content, asset_root / "textures");
                } else {
                    log_error("hot reload rejected: {}", error);
                }
            };
            std::error_code ec;
            auto mtime = std::filesystem::last_write_time(enemies_path, ec);
            if (!ec && mtime != enemies_mtime) {
                enemies_mtime = mtime;
                reload_file(enemies_path, &ContentDB::load_enemies);
            }
            mtime = std::filesystem::last_write_time(items_path, ec);
            if (!ec && mtime != items_mtime) {
                items_mtime = mtime;
                reload_file(items_path, &ContentDB::load_items);
            }
        }

        // Master volume, applied live from the settings cvar.
        if (snd_volume.value != applied_volume) {
            applied_volume = snd_volume.value;
            audio.set_volume(applied_volume);
        }

        // Menu navigation, mouse hit-testing and action dispatch.
        if (menu.active()) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            const glm::vec2 mvp{static_cast<float>(pw), static_cast<float>(ph)};
            if (!SDL_GetWindowRelativeMouseMode(window)) {
                float wx = 0.0f, wy = 0.0f;
                SDL_GetMouseState(&wx, &wy);
                int lw = 0, lh = 0;
                SDL_GetWindowSize(window, &lw, &lh);
                const glm::vec2 mpx{wx * (lw > 0 ? mvp.x / static_cast<float>(lw) : 1.0f),
                                    wy * (lh > 0 ? mvp.y / static_cast<float>(lh) : 1.0f)};
                menu_input.mouse_px = mpx;
                menu_input.mouse_moved = glm::distance(mpx, last_mouse_px) > 0.5f;
                last_mouse_px = mpx;
            }
            switch (menu.update(menu_input, mvp)) {
            case MenuAction::StartRun:
            case MenuAction::Restart:
                regenerate(seed + 1);
                enter_playing();
                break;
            case MenuAction::Resume:
                enter_playing();
                break;
            case MenuAction::QuitToTitle:
                regenerate(seed + 1);
                open_menu(MenuScreen::Title, GamePhase::Title);
                break;
            case MenuAction::QuitGame:
                running = false;
                break;
            case MenuAction::None:
                break;
            }
        }

        // The simulation only advances while actively playing.
        if (phase == GamePhase::Playing) {
            acc += frame_dt;
            while (acc >= kTickDt) {
                const PlayerCmd cmd = input.make_cmd(cam_yaw, cam_pitch);
                world.tick(cmd, static_cast<float>(kTickDt));
                acc -= kTickDt;
            }
            if (world.player_dead) {
                open_menu(MenuScreen::Death, GamePhase::Dead); // death → death screen
                menu.set_status_line(std::format("FINAL SCORE  {}", world.score));
            }
        } else {
            acc = 0.0;
        }

        // Sounds and combat feedback both ride the telemetry stream — one
        // event system for everything.
        world.telem.drain_since(telem_cursor, fresh_events);
        for (const TelemetryEvent& ev : fresh_events) {
            switch (ev.type) {
            case EvType::PlayerAttack:
                audio.play("swing");
                if ((ev.flags & 1u) && fx_hitmarker.as_bool()) {
                    fx_hitmarker_t = 1.0f;
                    fx_hitmarker_kill = false;
                    fx_kick.y -= 0.006f * fx_shake.value; // tiny forward punch
                }
                break;
            case EvType::ProjectileFired: audio.play("bolt_fire"); break;
            case EvType::ProjectileHit:
                if (ev.def != 0xffff) {
                    audio.play("hit");
                    if (fx_hitmarker.as_bool()) {
                        fx_hitmarker_t = 1.0f;
                        fx_hitmarker_kill = false;
                    }
                }
                break;
            case EvType::PlayerDamaged: {
                audio.play("hurt");
                fx_indicators.push_back({ev.yaw, 1.0f});
                fx_chip_delay = 0.6f;
                // Kick the camera up and away from the hit.
                const float rel = indicator_screen_rot(ev.yaw, cam_yaw);
                fx_kick.x += -std::sin(rel) * 0.02f * fx_shake.value;
                fx_kick.y += 0.022f * fx_shake.value;
                break;
            }
            case EvType::EnemyKilled:
                audio.play("kill");
                if (fx_hitmarker.as_bool()) {
                    fx_hitmarker_t = 1.0f;
                    fx_hitmarker_kill = true;
                }
                break;
            case EvType::PlayerDash: audio.play("dash"); break;
            default: break;
            }
        }

        // Per-frame feedback decay + low-health heartbeat.
        {
            const auto fdt = static_cast<float>(frame_dt);
            run_time += frame_dt;
            for (DamageIndicator& ind : fx_indicators) {
                ind.t -= fdt / std::max(fx_indicator_ttl.value, 0.05f);
            }
            std::erase_if(fx_indicators, [](const DamageIndicator& i) { return i.t <= 0.0f; });
            fx_hitmarker_t =
                std::max(0.0f, fx_hitmarker_t - fdt / (fx_hitmarker_kill ? 0.35f : 0.18f));
            fx_kick *= std::exp(-fdt * 10.0f);

            const auto& php = world.reg.get<Health>(world.player);
            if (fx_chip_hp < php.hp) {
                fx_chip_hp = php.hp; // heal / restart snaps the chip up instantly
            }
            fx_chip_delay = std::max(0.0f, fx_chip_delay - fdt);
            if (fx_chip_delay <= 0.0f && fx_chip_hp > php.hp) {
                fx_chip_hp =
                    std::max(php.hp, fx_chip_hp - (fx_chip_hp - php.hp + 20.0f) * fdt * 3.0f);
            }

            const float hfrac = php.max_hp > 0.0f ? php.hp / php.max_hp : 0.0f;
            if (!world.player_dead && hfrac < fx_lowhp.value) {
                fx_heartbeat_timer -= frame_dt;
                if (fx_heartbeat_timer <= 0.0) {
                    audio.play("heartbeat");
                    const float severity = 1.0f - hfrac / fx_lowhp.value;
                    fx_heartbeat_timer = 1.1 - 0.65 * static_cast<double>(severity);
                }
            } else {
                fx_heartbeat_timer = 0.0;
            }
        }

        if (world.player_dead && !was_dead) {
            was_dead = true;
            write_telemetry("death");
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
        view.camera.yaw = cam_yaw + fx_kick.x; // render-only kick; sim aim unaffected
        view.camera.pitch = std::clamp(cam_pitch + fx_kick.y, -kMaxPitch, kMaxPitch);
        if (phase == GamePhase::Title) {
            // Slow idle spin at the spawn point as a live backdrop for the title.
            view.camera.yaw = static_cast<float>(run_time) * 0.25f;
            view.camera.pitch = -0.08f;
        }
        view.camera.fov_deg = std::clamp(r_fov.value, 66.0f, 110.0f);
        view.time = run_time;
        for (auto [e, enemy, etr, eprev] :
             world.reg.view<const Enemy, const Transform, const PrevTransform>().each()) {
            const EnemyDef& def = world.content.enemies[enemy.def];
            const int layer = sprite_atlas.layer_of(def.sprite);
            if (layer < 0) {
                continue;
            }
            const glm::vec2 ep = glm::mix(eprev.pos, etr.pos, alpha);
            const HurtFlash* flash = world.reg.try_get<HurtFlash>(e);
            view.sprites.push_back({{ep.x, 0.0f, ep.y},
                                    def.sprite_size,
                                    static_cast<float>(layer),
                                    flash ? flash->t : 0.0f});
        }
        // Floor pickups: gently bobbing billboards.
        for (auto [e, pickup, ptr] : world.reg.view<const Pickup, const Transform>().each()) {
            const ItemDef& def = world.content.items[pickup.item];
            const int layer = sprite_atlas.layer_of(def.sprite);
            if (layer < 0) {
                continue;
            }
            const float bob =
                0.1f + 0.05f * std::sin(static_cast<float>(run_time) * 2.4f + pickup.bob_phase);
            view.sprites.push_back({{ptr.pos.x, bob, ptr.pos.y},
                                    def.sprite_size,
                                    static_cast<float>(layer),
                                    0.0f});
        }

        // All projectiles render as the glowy "bolt"; enemy shots are tinted
        // red via the sprite flash channel to read as a threat.
        const int bolt_layer = sprite_atlas.layer_of("bolt");
        if (bolt_layer >= 0) {
            for (auto [e, proj, ptr, pprev] :
                 world.reg.view<const Projectile, const Transform, const PrevTransform>().each()) {
                const glm::vec2 pp = glm::mix(pprev.pos, ptr.pos, alpha);
                // Bolts fly at eye height; sprite centers are feet, so drop half.
                view.sprites.push_back({{pp.x, r_eye_height.value - 0.125f, pp.y},
                                        {0.25f, 0.25f},
                                        static_cast<float>(bolt_layer),
                                        proj.team == Team::Enemy ? 1.0f : 0.0f});
            }
        }

        // HUD on top of the world, under the debug UI.
        {
            const auto& pl = world.reg.get<Player>(world.player);
            const auto& hp = world.reg.get<Health>(world.player);
            HudState hud;
            hud.hp = hp.hp;
            hud.max_hp = hp.max_hp;
            hud.swing_anim = pl.swing_anim;
            hud.cast_anim = pl.cast_anim;
            hud.hurt_flash = pl.hurt_flash;
            const CVar* dash_cd = cvar_find("sv.dash_cooldown");
            hud.dash_cooldown01 =
                dash_cd && dash_cd->value > 0.0f ? pl.dash_cooldown / dash_cd->value : 0.0f;
            hud.dead = world.player_dead;
            hud.score = world.score;
            hud.cam_yaw = cam_yaw;
            hud.time = run_time;
            hud.chip_hp = fx_chip_hp;
            hud.hitmarker_t = fx_hitmarker_t;
            hud.hitmarker_kill = fx_hitmarker_kill;
            hud.lowhp_threshold = std::clamp(fx_lowhp.value, 0.0f, 1.0f);
            hud.indicators = fx_indicators;
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (phase != GamePhase::Title) { // no gameplay HUD behind the title
                build_hud(view, hud, {static_cast<float>(pw), static_cast<float>(ph)}, &font);
            }
        }

        // Menus draw on top of the (dimmed) world (viewport set by update()).
        if (menu.active()) {
            menu.render(view, font);
        }
        renderer.render(view);

        if (cfg_.max_ticks >= 0 && static_cast<int64_t>(world.tick_count) >= cfg_.max_ticks) {
            running = false;
        }
    }

    write_telemetry(world.player_dead ? "death" : "quit");
    con_unregister_all();
    audio.shutdown();
    ui.shutdown();
    renderer.shutdown();
    return 0;
}

} // namespace ds
