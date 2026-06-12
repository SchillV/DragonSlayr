#version 450
// SDL_GPU SPIR-V binding conventions (do not deviate — pipeline creation fails):
//   vertex stage:   sampled textures set=0, uniform buffers set=1
//   fragment stage: sampled textures set=2, uniform buffers set=3
//
// 2D screen-space quads (viewmodel + HUD): instanced, pixel coordinates with
// a top-left origin, depth off, alpha blend on.

layout(location = 0) in vec2 i_pos;     // pixels, top-left of the quad
layout(location = 1) in vec2 i_size;    // pixels
layout(location = 2) in vec4 i_uv_rect; // u0 v0 u1 v1
layout(location = 3) in float i_layer;
layout(location = 4) in vec4 i_color;   // tint * texture

layout(set = 1, binding = 0) uniform OverlayUBO {
    vec4 viewport; // xy = viewport size in pixels
} u;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_layer;
layout(location = 2) out vec4 v_color;

const vec2 kCorner[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    vec2 c = kCorner[gl_VertexIndex];
    vec2 px = i_pos + c * i_size;
    vec2 ndc = vec2(px.x / u.viewport.x * 2.0 - 1.0, 1.0 - px.y / u.viewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = mix(i_uv_rect.xy, i_uv_rect.zw, c);
    v_layer = i_layer;
    v_color = i_color;
}
