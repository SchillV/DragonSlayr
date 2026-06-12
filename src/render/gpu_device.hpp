#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <filesystem>
#include <span>

namespace ds {

struct Image;

// Thin RAII-ish wrapper over SDL_GPUDevice plus the handful of upload/create
// helpers the renderer needs. One-shot uploads are fine at our asset sizes.
class GpuDevice {
public:
    bool init(SDL_Window* window, bool debug);
    void shutdown(SDL_Window* window);

    SDL_GPUDevice* handle() const { return device_; }
    SDL_GPUTextureFormat depth_format() const { return depth_format_; }

    SDL_GPUShader* load_shader(const std::filesystem::path& spv_path, SDL_GPUShaderStage stage,
                               uint32_t num_samplers, uint32_t num_uniform_buffers);
    SDL_GPUBuffer* create_buffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t size);
    // All layers must share dimensions. RGBA8, no mips, sampler usage.
    SDL_GPUTexture* create_texture_array(std::span<const Image> layers);
    SDL_GPUTexture* create_depth_target(uint32_t w, uint32_t h);
    SDL_GPUSampler* create_nearest_sampler();
    SDL_GPUSampler* create_clamp_sampler(); // nearest + clamp-to-edge (overlay)

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
};

} // namespace ds
