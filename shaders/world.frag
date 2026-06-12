#version 450
// SDL_GPU SPIR-V binding conventions (do not deviate — pipeline creation fails):
//   vertex stage:   sampled textures set=0, uniform buffers set=1
//   fragment stage: sampled textures set=2, uniform buffers set=3

layout(set = 2, binding = 0) uniform sampler2DArray u_atlas;

layout(set = 3, binding = 0) uniform FogUBO {
    vec4 fog_color_density; // rgb = fog color, w = exp density
} u_fog;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_layer;
layout(location = 2) in float v_shade;
layout(location = 3) in float v_view_depth;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 tex = texture(u_atlas, vec3(v_uv, v_layer)).rgb;
    vec3 lit = tex * v_shade;
    float fog = clamp(1.0 - exp(-v_view_depth * u_fog.fog_color_density.w), 0.0, 1.0);
    out_color = vec4(mix(lit, u_fog.fog_color_density.rgb, fog), 1.0);
}
