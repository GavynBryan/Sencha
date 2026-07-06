#version 450

// Bright-pass: keep only what exceeds the threshold (per channel), so only the
// HDR-bright active selection (authored > 1.0) blooms while solid bodies (< 1.0) do
// not. Reads the scene color via the bindless image array (index in the push constant).
layout(location = 0) in  vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(push_constant) uniform PC {
    uint  srcIndex;
    float threshold;
    float intensity;
    float radius;
    vec2  texel;
} pc;

void main()
{
    vec3 c = texture(uTextures[pc.srcIndex], vUv).rgb;
    outColor = vec4(max(c - pc.threshold, 0.0), 1.0);
}
