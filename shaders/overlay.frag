#version 450
// SDL_GPU SPIR-V binding conventions (do not deviate — pipeline creation fails):
//   vertex stage:   sampled textures set=0, uniform buffers set=1
//   fragment stage: sampled textures set=2, uniform buffers set=3

layout(set = 2, binding = 0) uniform sampler2DArray u_overlay;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_layer;
layout(location = 2) in vec4 v_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_overlay, vec3(v_uv, v_layer)) * v_color;
}
