#pragma once

#include <glm/glm.hpp>

namespace ds {

struct CameraView {
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;   // radians, 0 = +X, counter-clockwise on the XZ plane
    float pitch = 0.0f; // radians, positive looks up
    float fov_deg = 75.0f;
};

// Interpolated, render-ready snapshot of one frame. The renderer never touches
// the sim; everything it needs arrives through this struct.
struct FrameView {
    CameraView camera;
    double time = 0.0; // seconds since app start (debug/animation)
};

} // namespace ds
