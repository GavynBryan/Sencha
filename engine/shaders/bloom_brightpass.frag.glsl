#version 450
#extension GL_GOOGLE_include_directive : require

#include "bloom.glsli"

// Each source texel is thresholded on its own before the block is averaged: a
// filtered tap would average a thin line with its dark neighbour first, and
// lose it wherever the line falls between two texels.
void main()
{
    ivec2 last = textureSize(uTextures[pc.sourceIndex], 0) - 1;
    ivec2 origin = ivec2(gl_FragCoord.xy) * int(pc.gather);
    vec3 sum = vec3(0.0);
    for (uint y = 0u; y < pc.gather; ++y)
        for (uint x = 0u; x < pc.gather; ++x)
        {
            vec3 c = texelFetch(uTextures[pc.sourceIndex], min(origin + ivec2(x, y), last), 0).rgb;
            sum += max(c - pc.threshold, 0.0);
        }
    outColor = vec4(sum / float(pc.gather * pc.gather), 1.0);
}
