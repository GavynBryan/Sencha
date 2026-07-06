#version 450

// Separable Gaussian blur (9 taps). The push constant's texel holds the 1-texel step
// direction ((1/w,0) horizontal or (0,1/h) vertical) and radius scales it. Reads the
// previous bloom target via the bindless array.
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

const float kWeight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 step = pc.texel * max(pc.radius, 0.0);
    vec3 c = texture(uTextures[pc.srcIndex], vUv).rgb * kWeight[0];
    for (int i = 1; i < 5; ++i)
    {
        c += texture(uTextures[pc.srcIndex], vUv + step * float(i)).rgb * kWeight[i];
        c += texture(uTextures[pc.srcIndex], vUv - step * float(i)).rgb * kWeight[i];
    }
    outColor = vec4(c, 1.0);
}
