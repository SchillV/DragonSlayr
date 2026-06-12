#include "render/gpu_renderer.hpp"

#include "core/log.hpp"
#include "render/debug_ui.hpp"
#include "render/texture_load.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <utility>

namespace ds {

namespace {

struct CameraUbo {
    glm::mat4 view_proj;
};

struct SpriteCameraUbo {
    glm::mat4 view_proj;
    glm::vec4 cam_right;
};

struct FogUbo {
    glm::vec4 fog_color_density;
};

} // namespace

GpuRenderer::GpuRenderer(std::filesystem::path shader_dir) : shader_dir_(std::move(shader_dir)) {}

bool GpuRenderer::init(SDL_Window* window) {
    window_ = window;
    if (!dev_.init(window, /*debug=*/true)) {
        return false;
    }
    nearest_sampler_ = dev_.create_nearest_sampler();
    if (!nearest_sampler_ || !create_world_pipeline() || !create_sprite_pipeline()) {
        return false;
    }
    ensure_sprite_capacity(256);
    return true;
}

void GpuRenderer::shutdown() {
    SDL_GPUDevice* d = dev_.handle();
    if (!d) {
        return;
    }
    SDL_WaitForGPUIdle(d);
    if (depth_tex_) SDL_ReleaseGPUTexture(d, depth_tex_);
    if (world_vbuf_) SDL_ReleaseGPUBuffer(d, world_vbuf_);
    if (world_ibuf_) SDL_ReleaseGPUBuffer(d, world_ibuf_);
    if (world_atlas_) SDL_ReleaseGPUTexture(d, world_atlas_);
    if (sprite_instances_) SDL_ReleaseGPUBuffer(d, sprite_instances_);
    if (sprite_transfer_) SDL_ReleaseGPUTransferBuffer(d, sprite_transfer_);
    if (sprite_atlas_) SDL_ReleaseGPUTexture(d, sprite_atlas_);
    if (nearest_sampler_) SDL_ReleaseGPUSampler(d, nearest_sampler_);
    if (world_pipeline_) SDL_ReleaseGPUGraphicsPipeline(d, world_pipeline_);
    if (sprite_pipeline_) SDL_ReleaseGPUGraphicsPipeline(d, sprite_pipeline_);
    depth_tex_ = nullptr;
    world_vbuf_ = world_ibuf_ = nullptr;
    world_atlas_ = sprite_atlas_ = nullptr;
    sprite_instances_ = nullptr;
    sprite_transfer_ = nullptr;
    nearest_sampler_ = nullptr;
    world_pipeline_ = sprite_pipeline_ = nullptr;
    dev_.shutdown(window_);
}

SDL_GPUTextureFormat GpuRenderer::swapchain_format() const {
    return SDL_GetGPUSwapchainTextureFormat(dev_.handle(), window_);
}

bool GpuRenderer::create_world_pipeline() {
    SDL_GPUShader* vs = dev_.load_shader(shader_dir_ / "world.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX,
                                         /*samplers=*/0, /*ubos=*/1);
    SDL_GPUShader* fs = dev_.load_shader(shader_dir_ / "world.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT,
                                         /*samplers=*/1, /*ubos=*/1);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(dev_.handle(), vs);
        if (fs) SDL_ReleaseGPUShader(dev_.handle(), fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd{};
    vbd.slot = 0;
    vbd.pitch = sizeof(WorldVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[4]{};
    attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(WorldVertex, pos)};
    attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(WorldVertex, uv)};
    attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(WorldVertex, layer)};
    attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(WorldVertex, shade)};

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = swapchain_format();

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.vertex_input_state.num_vertex_attributes = 4;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // NONE until the owner confirms winding on real hardware, then BACK.
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = dev_.depth_format();
    pi.target_info.has_depth_stencil_target = true;

    world_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev_.handle(), &pi);
    SDL_ReleaseGPUShader(dev_.handle(), vs);
    SDL_ReleaseGPUShader(dev_.handle(), fs);
    if (!world_pipeline_) {
        log_error("world pipeline creation failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool GpuRenderer::create_sprite_pipeline() {
    SDL_GPUShader* vs = dev_.load_shader(shader_dir_ / "sprite.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX,
                                         /*samplers=*/0, /*ubos=*/1);
    SDL_GPUShader* fs = dev_.load_shader(shader_dir_ / "sprite.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT,
                                         /*samplers=*/1, /*ubos=*/1);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(dev_.handle(), vs);
        if (fs) SDL_ReleaseGPUShader(dev_.handle(), fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd{};
    vbd.slot = 0;
    vbd.pitch = sizeof(SpriteInstance);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_GPUVertexAttribute attrs[4]{};
    attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SpriteInstance, center)};
    attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(SpriteInstance, size)};
    attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(SpriteInstance, layer)};
    attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(SpriteInstance, flash)};

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = swapchain_format();

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.vertex_input_state.num_vertex_attributes = 4;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // billboards are double-sided
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true; // alpha-discard makes this safe
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = dev_.depth_format();
    pi.target_info.has_depth_stencil_target = true;

    sprite_pipeline_ = SDL_CreateGPUGraphicsPipeline(dev_.handle(), &pi);
    SDL_ReleaseGPUShader(dev_.handle(), vs);
    SDL_ReleaseGPUShader(dev_.handle(), fs);
    if (!sprite_pipeline_) {
        log_error("sprite pipeline creation failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

void GpuRenderer::set_world_textures(std::span<const Image> layers) {
    if (world_atlas_) {
        SDL_ReleaseGPUTexture(dev_.handle(), world_atlas_);
    }
    world_atlas_ = dev_.create_texture_array(layers);
}

void GpuRenderer::set_sprite_textures(std::span<const Image> layers) {
    if (sprite_atlas_) {
        SDL_ReleaseGPUTexture(dev_.handle(), sprite_atlas_);
        sprite_atlas_ = nullptr;
    }
    if (!layers.empty()) {
        sprite_atlas_ = dev_.create_texture_array(layers);
    }
}

void GpuRenderer::set_dungeon_mesh(const MeshData& mesh) {
    SDL_GPUDevice* d = dev_.handle();
    if (world_vbuf_) SDL_ReleaseGPUBuffer(d, world_vbuf_);
    if (world_ibuf_) SDL_ReleaseGPUBuffer(d, world_ibuf_);
    world_vbuf_ = world_ibuf_ = nullptr;
    world_index_count_ = static_cast<uint32_t>(mesh.indices.size());
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }
    world_vbuf_ = dev_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, mesh.vertices.data(),
                                     static_cast<uint32_t>(mesh.vertices.size() * sizeof(WorldVertex)));
    world_ibuf_ = dev_.create_buffer(SDL_GPU_BUFFERUSAGE_INDEX, mesh.indices.data(),
                                     static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t)));
}

void GpuRenderer::ensure_depth_target(uint32_t w, uint32_t h) {
    if (depth_tex_ && depth_w_ == w && depth_h_ == h) {
        return;
    }
    if (depth_tex_) {
        SDL_ReleaseGPUTexture(dev_.handle(), depth_tex_);
    }
    depth_tex_ = dev_.create_depth_target(w, h);
    depth_w_ = w;
    depth_h_ = h;
}

void GpuRenderer::ensure_sprite_capacity(uint32_t count) {
    if (count <= sprite_capacity_) {
        return;
    }
    uint32_t cap = sprite_capacity_ ? sprite_capacity_ : 256;
    while (cap < count) {
        cap *= 2;
    }
    SDL_GPUDevice* d = dev_.handle();
    if (sprite_instances_) SDL_ReleaseGPUBuffer(d, sprite_instances_);
    if (sprite_transfer_) SDL_ReleaseGPUTransferBuffer(d, sprite_transfer_);

    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = cap * sizeof(SpriteInstance);
    sprite_instances_ = SDL_CreateGPUBuffer(d, &bi);

    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = cap * sizeof(SpriteInstance);
    sprite_transfer_ = SDL_CreateGPUTransferBuffer(d, &ti);

    sprite_capacity_ = cap;
}

void GpuRenderer::render(const FrameView& view) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_.handle());
    if (!cmd) {
        log_error("SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchain, &w, &h) || !swapchain) {
        // Minimized or lost swapchain — submit the empty buffer and move on.
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }
    ensure_depth_target(w, h);

    ImDrawData* ui_data = debug_ui_ ? debug_ui_->prepare(cmd) : nullptr;

    // Per-frame sprite instance upload (before any render pass).
    const auto sprite_count = static_cast<uint32_t>(view.sprites.size());
    const bool draw_sprites = sprite_pipeline_ && sprite_atlas_ && sprite_count > 0;
    if (draw_sprites) {
        ensure_sprite_capacity(sprite_count);
        void* map = SDL_MapGPUTransferBuffer(dev_.handle(), sprite_transfer_, /*cycle=*/true);
        std::memcpy(map, view.sprites.data(), sprite_count * sizeof(SpriteInstance));
        SDL_UnmapGPUTransferBuffer(dev_.handle(), sprite_transfer_);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = sprite_transfer_;
        SDL_GPUBufferRegion dst{};
        dst.buffer = sprite_instances_;
        dst.size = sprite_count * sizeof(SpriteInstance);
        SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/true);
        SDL_EndGPUCopyPass(cp);
    }

    // Shared camera math.
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    const glm::vec3 forward{std::cos(view.camera.yaw) * std::cos(view.camera.pitch),
                            std::sin(view.camera.pitch),
                            std::sin(view.camera.yaw) * std::cos(view.camera.pitch)};
    const glm::mat4 view_mat =
        glm::lookAt(view.camera.pos, view.camera.pos + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(view.camera.fov_deg), aspect, 0.05f, 100.0f);
    const glm::mat4 view_proj = proj * view_mat;
    const glm::vec4 cam_right{-std::sin(view.camera.yaw), 0.0f, std::cos(view.camera.yaw), 0.0f};
    const FogUbo fog{fog_color_density_};

    // --- main pass: world geometry + billboards --------------------------------
    SDL_GPUColorTargetInfo color{};
    color.texture = swapchain;
    color.clear_color = {fog_color_density_.r, fog_color_density_.g, fog_color_density_.b, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_tex_;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);

    if (world_pipeline_ && world_vbuf_ && world_ibuf_ && world_atlas_ && world_index_count_ > 0) {
        SDL_BindGPUGraphicsPipeline(pass, world_pipeline_);

        SDL_GPUBufferBinding vb{};
        vb.buffer = world_vbuf_;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{};
        ib.buffer = world_ibuf_;
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUTextureSamplerBinding ts{};
        ts.texture = world_atlas_;
        ts.sampler = nearest_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, &ts, 1);

        const CameraUbo cam{view_proj};
        SDL_PushGPUVertexUniformData(cmd, 0, &cam, sizeof(cam));
        SDL_PushGPUFragmentUniformData(cmd, 0, &fog, sizeof(fog));

        SDL_DrawGPUIndexedPrimitives(pass, world_index_count_, 1, 0, 0, 0);
    }

    if (draw_sprites) {
        SDL_BindGPUGraphicsPipeline(pass, sprite_pipeline_);

        SDL_GPUBufferBinding vb{};
        vb.buffer = sprite_instances_;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

        SDL_GPUTextureSamplerBinding ts{};
        ts.texture = sprite_atlas_;
        ts.sampler = nearest_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, &ts, 1);

        const SpriteCameraUbo cam{view_proj, cam_right};
        SDL_PushGPUVertexUniformData(cmd, 0, &cam, sizeof(cam));
        SDL_PushGPUFragmentUniformData(cmd, 0, &fog, sizeof(fog));

        SDL_DrawGPUPrimitives(pass, 6, sprite_count, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    // --- UI pass (loads the world image, no depth) ----------------------------
    if (ui_data && debug_ui_) {
        SDL_GPUColorTargetInfo ui_color{};
        ui_color.texture = swapchain;
        ui_color.load_op = SDL_GPU_LOADOP_LOAD;
        ui_color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* ui_pass = SDL_BeginGPURenderPass(cmd, &ui_color, 1, nullptr);
        debug_ui_->draw(ui_data, cmd, ui_pass);
        SDL_EndGPURenderPass(ui_pass);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
}

} // namespace ds
