#version 450

// Composite: output the blurred bloom scaled by intensity. Drawn with additive blend
// over the scene color target, so it adds the glow without darkening anything.
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
    outColor = vec4(texture(uTextures[pc.srcIndex], vUv).rgb * pc.intensity, 0.0);
}
