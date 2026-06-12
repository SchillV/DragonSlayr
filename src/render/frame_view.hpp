#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace ds {

struct CameraView {
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;   // radians, 0 = +X, counter-clockwise on the XZ plane
    float pitch = 0.0f; // radians, positive looks up
    float fov_deg = 75.0f;
};

// One billboard. Layout mirrors the sprite shader's per-instance attributes.
struct SpriteInstance {
    glm::vec3 center{0.0f}; // feet position (sprite grows upward)
    glm::vec2 size{0.9f};
    float layer = 0.0f; // sprite texture array layer
    float flash = 0.0f; // 0..1 hurt flash
};
static_assert(sizeof(SpriteInstance) == 7 * sizeof(float));

// One 2D screen-space quad. Layout mirrors the overlay shader's attributes.
struct OverlayQuad {
    glm::vec2 pos{0.0f};  // pixels, top-left origin
    glm::vec2 size{0.0f}; // pixels
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
    float layer = 0.0f;   // overlay texture array layer (0 = white)
    glm::vec3 _pad{0.0f}; // keeps color vec4-aligned for the GPU layout
    glm::vec4 color{1.0f};
};
static_assert(sizeof(OverlayQuad) == 16 * sizeof(float));

// Interpolated, render-ready snapshot of one frame. The renderer never touches
// the sim; everything it needs arrives through this struct.
struct FrameView {
    CameraView camera;
    double time = 0.0; // seconds since app start (debug/animation)
    std::vector<SpriteInstance> sprites;
    std::vector<OverlayQuad> overlay; // drawn in order, after the world
};

} // namespace ds
