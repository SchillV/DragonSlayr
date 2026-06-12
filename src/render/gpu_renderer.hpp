#pragma once

#include "render/gpu_device.hpp"
#include "render/renderer.hpp"

#include <filesystem>

namespace ds {

class DebugUi;

class GpuRenderer final : public IRenderer {
public:
    explicit GpuRenderer(std::filesystem::path shader_dir);

    bool init(SDL_Window* window) override;
    void shutdown() override;
    void set_world_textures(std::span<const Image> layers) override;
    void set_sprite_textures(std::span<const Image> layers) override;
    void set_overlay_textures(std::span<const Image> layers) override;
    void set_dungeon_mesh(const MeshData& mesh) override;
    void render(const FrameView& view) override;

    SDL_GPUDevice* device() const { return dev_.handle(); }
    SDL_GPUTextureFormat swapchain_format() const;
    void set_debug_ui(DebugUi* ui) { debug_ui_ = ui; }

private:
    bool create_world_pipeline();
    bool create_sprite_pipeline();
    bool create_overlay_pipeline();
    void ensure_depth_target(uint32_t w, uint32_t h);
    void ensure_sprite_capacity(uint32_t count);
    void ensure_overlay_capacity(uint32_t count);

    std::filesystem::path shader_dir_;
    SDL_Window* window_ = nullptr;
    DebugUi* debug_ui_ = nullptr;
    GpuDevice dev_;

    SDL_GPUGraphicsPipeline* world_pipeline_ = nullptr;
    SDL_GPUSampler* nearest_sampler_ = nullptr;
    SDL_GPUTexture* world_atlas_ = nullptr;
    SDL_GPUBuffer* world_vbuf_ = nullptr;
    SDL_GPUBuffer* world_ibuf_ = nullptr;
    uint32_t world_index_count_ = 0;

    SDL_GPUGraphicsPipeline* sprite_pipeline_ = nullptr;
    SDL_GPUTexture* sprite_atlas_ = nullptr;
    SDL_GPUBuffer* sprite_instances_ = nullptr;
    SDL_GPUTransferBuffer* sprite_transfer_ = nullptr;
    uint32_t sprite_capacity_ = 0;

    SDL_GPUGraphicsPipeline* overlay_pipeline_ = nullptr;
    SDL_GPUTexture* overlay_atlas_ = nullptr;
    SDL_GPUSampler* clamp_sampler_ = nullptr;
    SDL_GPUBuffer* overlay_instances_ = nullptr;
    SDL_GPUTransferBuffer* overlay_transfer_ = nullptr;
    uint32_t overlay_capacity_ = 0;

    SDL_GPUTexture* depth_tex_ = nullptr;
    uint32_t depth_w_ = 0;
    uint32_t depth_h_ = 0;

    glm::vec4 fog_color_density_{0.07f, 0.05f, 0.10f, 0.09f};
};

} // namespace ds
