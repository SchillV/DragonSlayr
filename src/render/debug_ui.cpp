#include "render/debug_ui.hpp"

#include "core/log.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>

namespace ds {

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
    return true;
}

void DebugUi::shutdown() {
    if (!initialized_) {
        return;
    }
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool DebugUi::process_event(const SDL_Event& ev) {
    if (!initialized_) {
        return false;
    }
    ImGui_ImplSDL3_ProcessEvent(&ev);
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
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

void DebugUi::build() {
    if (!initialized_ || !visible) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 130), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Performance")) {
        const float ms = ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::Text("%.2f ms  (%.0f fps)", ms, ms > 0.0f ? 1000.0f / ms : 0.0f);
        const float worst = *std::max_element(std::begin(frame_history_), std::end(frame_history_));
        ImGui::PlotLines("##frametimes", frame_history_, static_cast<int>(std::size(frame_history_)),
                         frame_cursor_, nullptr, 0.0f, std::max(worst, 20.0f), ImVec2(-1, 60));
    }
    ImGui::End();
}

ImDrawData* DebugUi::prepare(SDL_GPUCommandBuffer* cmd) {
    if (!initialized_ || !visible) {
        if (initialized_) {
            ImGui::Render(); // keep the frame lifecycle balanced even when hidden
        }
        return nullptr;
    }
    ImGui::Render();
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
