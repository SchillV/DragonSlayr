#include "render/debug_ui.hpp"

#include "core/cvar.hpp"
#include "core/log.hpp"
#include "sim/tilemap.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace ds {

namespace {
constexpr size_t kMaxConsoleLines = 400;
} // namespace

bool DebugUi::init(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat swapchain_format) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        log_error("ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = device;
    info.ColorTargetFormat = swapchain_format;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        log_error("ImGui_ImplSDLGPU3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    initialized_ = true;

    // Mirror the log into the console.
    log_set_sink([this](LogLevel level, std::string_view msg) {
        const char* tag = level == LogLevel::Error ? "[ERR] "
                          : level == LogLevel::Warn ? "[WRN] "
                                                    : "";
        push_console_line(std::string(tag) + std::string(msg));
    });
    return true;
}

void DebugUi::shutdown() {
    if (!initialized_) {
        return;
    }
    log_set_sink(nullptr);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool DebugUi::process_event(const SDL_Event& ev) {
    if (!initialized_) {
        return false;
    }
    // The console toggle works no matter who has keyboard focus.
    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_GRAVE && !ev.key.repeat) {
        console_open_ = !console_open_;
        console_focus_pending_ = console_open_;
        if (console_open_) {
            visible = true; // opening the console implies showing the overlay
        }
        return true;
    }
    ImGui_ImplSDL3_ProcessEvent(&ev);
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

bool DebugUi::wants_keyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUi::add_frame_sample(float frame_ms) {
    frame_history_[frame_cursor_] = frame_ms;
    frame_cursor_ = (frame_cursor_ + 1) % static_cast<int>(std::size(frame_history_));
}

void DebugUi::new_frame() {
    if (!initialized_) {
        return;
    }
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void DebugUi::build(const DebugUiState& state) {
    if (!initialized_ || !visible) {
        return;
    }
    if (!seed_input_synced_) {
        seed_input_ = state.seed;
        seed_input_synced_ = true;
    }
    build_performance_window(state);
    build_dungeon_window(state);
    build_console_window();
}

void DebugUi::build_performance_window(const DebugUiState& state) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 150), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Performance")) {
        const float ms = ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::Text("%.2f ms  (%.0f fps)", ms, ms > 0.0f ? 1000.0f / ms : 0.0f);
        ImGui::Text("player speed: %.2f tiles/s", state.player_speed);
        const float worst = *std::max_element(std::begin(frame_history_), std::end(frame_history_));
        ImGui::PlotLines("##frametimes", frame_history_, static_cast<int>(std::size(frame_history_)),
                         frame_cursor_, nullptr, 0.0f, std::max(worst, 20.0f), ImVec2(-1, 60));
    }
    ImGui::End();
}

void DebugUi::build_dungeon_window(const DebugUiState& state) {
    ImGui::SetNextWindowPos(ImVec2(10, 170), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dungeon")) {
        ImGui::End();
        return;
    }

    ImGui::InputScalar("seed", ImGuiDataType_U64, &seed_input_);
    ImGui::SameLine();
    if (ImGui::Button("Regenerate") && state.regenerate) {
        state.regenerate(seed_input_);
    }
    if (ImGui::Button("Next seed") && state.regenerate) {
        ++seed_input_;
        state.regenerate(seed_input_);
    }

    if (state.map) {
        const TileMap& map = *state.map;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float avail = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
        const float scale =
            std::max(1.0f, std::floor(avail / static_cast<float>(std::max(map.width(), 1))));

        const ImU32 col_wall = IM_COL32(150, 110, 70, 255);
        const ImU32 col_floor = IM_COL32(70, 70, 80, 255);
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                const Tile t = map.tiles.at(x, y);
                if (t == Tile::Void) continue;
                const ImVec2 a(origin.x + static_cast<float>(x) * scale,
                               origin.y + static_cast<float>(y) * scale);
                const ImVec2 b(a.x + scale, a.y + scale);
                draw->AddRectFilled(a, b, t == Tile::Wall ? col_wall : col_floor);
            }
        }
        // Camera marker + facing line.
        const ImVec2 cam(origin.x + state.cam_pos.x * scale, origin.y + state.cam_pos.y * scale);
        const ImVec2 tip(cam.x + std::cos(state.cam_yaw) * scale * 2.0f,
                         cam.y + std::sin(state.cam_yaw) * scale * 2.0f);
        draw->AddLine(cam, tip, IM_COL32(255, 60, 60, 255), 2.0f);
        draw->AddCircleFilled(cam, std::max(scale * 0.5f, 2.0f), IM_COL32(255, 60, 60, 255));

        ImGui::Dummy(ImVec2(static_cast<float>(map.width()) * scale,
                            static_cast<float>(map.height()) * scale));
    }
    ImGui::End();
}

void DebugUi::push_console_line(std::string line) {
    // Feedback can be multiline (e.g. help) — split for the scroller.
    size_t start = 0;
    while (start <= line.size()) {
        const size_t nl = line.find('\n', start);
        const std::string_view part(line.data() + start,
                                    (nl == std::string::npos ? line.size() : nl) - start);
        if (!part.empty()) {
            console_lines_.emplace_back(part);
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (console_lines_.size() > kMaxConsoleLines) {
        console_lines_.erase(console_lines_.begin(),
                             console_lines_.begin() +
                                 static_cast<long>(console_lines_.size() - kMaxConsoleLines));
    }
    console_scroll_pending_ = true;
}

void DebugUi::build_console_window() {
    if (!console_open_) {
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y * 0.45f), ImGuiCond_Always);
    if (!ImGui::Begin("Console", &console_open_,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##scroll", ImVec2(0, -footer))) {
        for (const std::string& line : console_lines_) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (console_scroll_pending_) {
            ImGui::SetScrollHereY(1.0f);
            console_scroll_pending_ = false;
        }
    }
    ImGui::EndChild();

    if (console_focus_pending_) {
        ImGui::SetKeyboardFocusHere();
        console_focus_pending_ = false;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##input", console_input_, sizeof(console_input_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string line = console_input_;
        if (!line.empty()) {
            push_console_line("> " + line);
            std::string feedback;
            con_execute(line, feedback);
            if (!feedback.empty()) {
                push_console_line(std::move(feedback));
            }
            console_input_[0] = '\0';
        }
        console_focus_pending_ = true; // keep typing
    }
    ImGui::End();
}

ImDrawData* DebugUi::prepare(SDL_GPUCommandBuffer* cmd) {
    if (!initialized_) {
        return nullptr;
    }
    ImGui::Render(); // keep the frame lifecycle balanced even when hidden
    if (!visible) {
        return nullptr;
    }
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) {
        return nullptr;
    }
    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
    return draw_data;
}

void DebugUi::draw(ImDrawData* draw_data, SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
    if (!initialized_ || !draw_data) {
        return;
    }
    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
}

} // namespace ds
