#version 450
// SDL_GPU SPIR-V binding conventions (do not deviate — pipeline creation fails):
//   vertex stage:   sampled textures set=0, uniform buffers set=1
//   fragment stage: sampled textures set=2, uniform buffers set=3

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_layer;
layout(location = 3) in float in_shade;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view_proj;
} u_cam;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_layer;
layout(location = 2) out float v_shade;
layout(location = 3) out float v_view_depth;

void main() {
    gl_Position = u_cam.view_proj * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_layer = in_layer;
    v_shade = in_shade;
    v_view_depth = gl_Position.w;
}
