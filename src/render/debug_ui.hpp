#pragma once

#include <SDL3/SDL_gpu.h>

struct SDL_Window;
union SDL_Event;
struct ImDrawData;

namespace ds {

// All Dear ImGui touchpoints live here so a backend break never bleeds into
// the rest of the renderer.
class DebugUi {
public:
    bool init(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat swapchain_format);
    void shutdown();

    // Returns true when ImGui wants the event (don't forward to the game).
    bool process_event(const SDL_Event& ev);

    void add_frame_sample(float frame_ms);
    void new_frame(); // both backend NewFrames + ImGui::NewFrame
    void build();     // assembles the debug windows for this frame

    // MANDATORY before the render pass that draws the UI (uploads vtx/idx).
    ImDrawData* prepare(SDL_GPUCommandBuffer* cmd);
    void draw(ImDrawData* draw_data, SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass);

    bool visible = true; // F2 toggles

private:
    bool initialized_ = false;
    float frame_history_[240] = {};
    int frame_cursor_ = 0;
};

} // namespace ds
