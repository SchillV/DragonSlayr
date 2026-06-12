#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>

struct SDL_Window;
union SDL_Event;
struct ImDrawData;

namespace ds {

struct TileMap;

// Everything the debug overlay shows or pokes, assembled fresh each frame.
struct DebugUiState {
    const TileMap* map = nullptr;
    glm::vec2 cam_pos{0.0f};
    float cam_yaw = 0.0f;
    uint64_t seed = 0;
    std::function<void(uint64_t)> regenerate; // invoked with the seed to generate
};

// All Dear ImGui touchpoints live here so a backend break never bleeds into
// the rest of the renderer.
class DebugUi {
public:
    bool init(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat swapchain_format);
    void shutdown();

    // Returns true when ImGui wants the event (don't forward to the game).
    bool process_event(const SDL_Event& ev);
    bool wants_keyboard() const;

    void add_frame_sample(float frame_ms);
    void new_frame(); // both backend NewFrames + ImGui::NewFrame
    void build(const DebugUiState& state);

    // MANDATORY before the render pass that draws the UI (uploads vtx/idx).
    ImDrawData* prepare(SDL_GPUCommandBuffer* cmd);
    void draw(ImDrawData* draw_data, SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass);

    bool visible = true; // F2 toggles

private:
    void build_performance_window();
    void build_dungeon_window(const DebugUiState& state);

    bool initialized_ = false;
    float frame_history_[240] = {};
    int frame_cursor_ = 0;
    uint64_t seed_input_ = 1;
    bool seed_input_synced_ = false;
};

} // namespace ds
