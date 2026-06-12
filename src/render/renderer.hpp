#pragma once

#include "render/frame_view.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

struct SDL_Window;

namespace ds {

struct Image;

struct WorldVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float layer; // texture array layer
    float shade; // per-face brightness (N/S vs E/W walls, floor, ceiling)
};

struct MeshData {
    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;
};

// The headless boundary: sim and game code only ever see this interface.
// NullRenderer keeps --headless runs (and the dev container, which has no GPU)
// on the exact same code path as windowed runs.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(SDL_Window* window) = 0;
    virtual void shutdown() = 0;
    // All layers must share dimensions; they become one RGBA8 texture array.
    virtual void set_world_textures(std::span<const Image> layers) = 0;
    virtual void set_sprite_textures(std::span<const Image> layers) = 0;
    virtual void set_overlay_textures(std::span<const Image> layers) = 0;
    virtual void set_dungeon_mesh(const MeshData& mesh) = 0;
    virtual void render(const FrameView& view) = 0;
};

class NullRenderer final : public IRenderer {
public:
    bool init(SDL_Window*) override { return true; }
    void shutdown() override {}
    void set_world_textures(std::span<const Image>) override {}
    void set_sprite_textures(std::span<const Image>) override {}
    void set_overlay_textures(std::span<const Image>) override {}
    void set_dungeon_mesh(const MeshData&) override {}
    void render(const FrameView&) override {}
};

} // namespace ds
