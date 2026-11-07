#version 450
#extension GL_GOOGLE_include_directive : require

#include "bloom.glsli"

// Nine taps along pc.texel, one axis per pass. A radius past 1 leaves gaps between
// taps that thin lines fall into, so reach comes from the plane's resolution.
const float kWeight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 step = pc.texel * max(pc.radius, 0.0);
    vec3 c = texture(uTextures[pc.sourceIndex], vUv).rgb * kWeight[0];
    for (int i = 1; i < 5; ++i)
    {
        c += texture(uTextures[pc.sourceIndex], vUv + step * float(i)).rgb * kWeight[i];
        c += texture(uTextures[pc.sourceIndex], vUv - step * float(i)).rgb * kWeight[i];
    }
    outColor = vec4(c, 1.0);
}
