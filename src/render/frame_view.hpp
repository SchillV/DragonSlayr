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

// Interpolated, render-ready snapshot of one frame. The renderer never touches
// the sim; everything it needs arrives through this struct.
struct FrameView {
    CameraView camera;
    double time = 0.0; // seconds since app start (debug/animation)
    std::vector<SpriteInstance> sprites;
};

} // namespace ds
