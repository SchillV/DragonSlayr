#version 450
// SDL_GPU SPIR-V binding conventions (do not deviate — pipeline creation fails):
//   vertex stage:   sampled textures set=0, uniform buffers set=1
//   fragment stage: sampled textures set=2, uniform buffers set=3
//
// Instanced cylindrical billboards: quad corners come from gl_VertexIndex,
// all attributes are per-instance. World-up locked so sprites never tilt
// when the camera pitches.

layout(location = 0) in vec3 i_center; // feet position
layout(location = 1) in vec2 i_size;   // width, height in tiles
layout(location = 2) in float i_layer;
layout(location = 3) in float i_flash;

layout(set = 1, binding = 0) uniform SpriteCameraUBO {
    mat4 view_proj;
    vec4 cam_right; // xz-plane right vector
} u_cam;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_layer;
layout(location = 2) out float v_flash;
layout(location = 3) out float v_view_depth;

const vec2 kCorner[6] = vec2[](
    vec2(-0.5, 1.0), vec2(0.5, 1.0), vec2(0.5, 0.0),
    vec2(-0.5, 1.0), vec2(0.5, 0.0), vec2(-0.5, 0.0));

void main() {
    vec2 c = kCorner[gl_VertexIndex];
    vec3 world = i_center + u_cam.cam_right.xyz * (c.x * i_size.x)
               + vec3(0.0, c.y * i_size.y, 0.0);
    gl_Position = u_cam.view_proj * vec4(world, 1.0);
    v_uv = vec2(c.x + 0.5, 1.0 - c.y);
    v_layer = i_layer;
    v_flash = i_flash;
    v_view_depth = gl_Position.w;
}
