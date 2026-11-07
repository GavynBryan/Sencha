#version 450
#extension GL_GOOGLE_include_directive : require

#include "bloom.glsli"

void main()
{
    outColor = vec4(max(texture(uTextures[pc.sourceIndex], vUv).rgb - pc.threshold, 0.0), 1.0);
}
