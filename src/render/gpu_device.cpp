#include "render/gpu_device.hpp"

#include "core/log.hpp"
#include "render/texture_load.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace ds {

bool GpuDevice::init(SDL_Window* window, bool debug) {
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, debug, nullptr);
    if (!device_) {
        log_error("SDL_CreateGPUDevice failed: {}", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
        log_error("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        return false;
    }
    depth_format_ = SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                                                 SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                        ? SDL_GPU_TEXTUREFORMAT_D24_UNORM
                        : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    log_info("GPU device created (driver: {})", SDL_GetGPUDeviceDriver(device_));
    return true;
}

void GpuDevice::shutdown(SDL_Window* window) {
    if (!device_) {
        return;
    }
    SDL_ReleaseWindowFromGPUDevice(device_, window);
    SDL_DestroyGPUDevice(device_);
    device_ = nullptr;
}

SDL_GPUShader* GpuDevice::load_shader(const std::filesystem::path& spv_path, SDL_GPUShaderStage stage,
                                      uint32_t num_samplers, uint32_t num_uniform_buffers) {
    std::ifstream f(spv_path, std::ios::binary);
    if (!f) {
        log_error("missing shader file: {}", spv_path.string());
        return nullptr;
    }
    const std::vector<uint8_t> code(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{});

    SDL_GPUShaderCreateInfo info{};
    info.code_size = code.size();
    info.code = code.data();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = num_samplers;
    info.num_uniform_buffers = num_uniform_buffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
    if (!shader) {
        log_error("SDL_CreateGPUShader failed ({}): {}", spv_path.string(), SDL_GetError());
    }
    return shader;
}

SDL_GPUBuffer* GpuDevice::create_buffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t size) {
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = size;
    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(device_, &bi);
    if (!buf) {
        log_error("SDL_CreateGPUBuffer failed: {}", SDL_GetError());
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    void* map = SDL_MapGPUTransferBuffer(device_, tb, false);
    std::memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    SDL_GPUBufferRegion dst{};
    dst.buffer = buf;
    dst.size = size;
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);
    return buf;
}

SDL_GPUTexture* GpuDevice::create_texture_array(std::span<const Image> layers) {
    if (layers.empty()) {
        return nullptr;
    }
    const uint32_t w = static_cast<uint32_t>(layers[0].width);
    const uint32_t h = static_cast<uint32_t>(layers[0].height);
    const uint32_t n = static_cast<uint32_t>(layers.size());
    const uint32_t layer_bytes = w * h * 4;
    for (const Image& img : layers) {
        if (static_cast<uint32_t>(img.width) != w || static_cast<uint32_t>(img.height) != h) {
            log_error("texture array layers must share dimensions ({}x{} expected)", w, h);
            return nullptr;
        }
    }

    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = n;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &ti);
    if (!texture) {
        log_error("SDL_CreateGPUTexture failed: {}", SDL_GetError());
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = layer_bytes * n;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbi);
    auto* map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device_, tb, false));
    for (uint32_t i = 0; i < n; ++i) {
        std::memcpy(map + static_cast<size_t>(i) * layer_bytes, layers[i].pixels.data(), layer_bytes);
    }
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    for (uint32_t i = 0; i < n; ++i) {
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        src.offset = i * layer_bytes;
        src.pixels_per_row = w;
        src.rows_per_layer = h;
        SDL_GPUTextureRegion dst{};
        dst.texture = texture;
        dst.layer = i;
        dst.w = w;
        dst.h = h;
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);
    return texture;
}

SDL_GPUTexture* GpuDevice::create_depth_target(uint32_t w, uint32_t h) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = depth_format_;
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_, &ti);
    if (!tex) {
        log_error("depth target creation failed: {}", SDL_GetError());
    }
    return tex;
}

SDL_GPUSampler* GpuDevice::create_nearest_sampler() {
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &si);
    if (!sampler) {
        log_error("sampler creation failed: {}", SDL_GetError());
    }
    return sampler;
}

SDL_GPUSampler* GpuDevice::create_clamp_sampler() {
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &si);
    if (!sampler) {
        log_error("sampler creation failed: {}", SDL_GetError());
    }
    return sampler;
}

} // namespace ds
