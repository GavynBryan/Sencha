#version 450

#include "tonemap.glsli"

// Takes the scene from scene-referred to display-referred linear light, the only
// place exposure and the shoulder are applied, and composites the outlines over the
// result. The target's hardware sRGB write is the frame's one encode; only a target
// with nothing to encode it encodes here.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kEncodeOutput = false;

layout(push_constant) uniform OutputPresentPC {
    uint sceneIndex;
    float exposure;
    float tonemapKnee;
    uint tonemapEnabled;
    uint outlineMaskIndex;
    uint outlineStyleIndex;
    float outlineMaxWidth;
} pc;

const uint kNoImage = 0xFFFFFFFFu;

vec3 EncodeSrgb(vec3 linear)
{
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(max(linear, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

// An outer outline, only on pixels no highlighted surface covers, so a highlighted
// object's material is never painted over. A fixed ring of taps at three radii up to
// the widest style; a style counts only within its own width, and the highest
// priority takes a pixel two styles compete for.
vec3 CompositeOutline(vec3 display, ivec2 pixel)
{
    ivec2 last = textureSize(uTextures[pc.outlineMaskIndex], 0) - ivec2(1);
    if (texelFetch(uTextures[pc.outlineMaskIndex], pixel, 0).r != 0.0)
        return display;

    const vec2 kDirections[8] = vec2[](vec2(1, 0), vec2(0.7071, 0.7071), vec2(0, 1), vec2(-0.7071, 0.7071),
                                       vec2(-1, 0), vec2(-0.7071, -0.7071), vec2(0, -1), vec2(0.7071, -0.7071));
    int winner = 0;
    float winnerPriority = 0.0;
    for (int ring = 1; ring <= 3; ++ring)
    {
        float radius = pc.outlineMaxWidth * float(ring) / 3.0;
        for (int i = 0; i < 8; ++i)
        {
            ivec2 tap = clamp(pixel + ivec2(round(kDirections[i] * radius)), ivec2(0), last);
            int slot = int(round(texelFetch(uTextures[pc.outlineMaskIndex], tap, 0).r * 255.0));
            if (slot == 0)
                continue;
            vec4 style = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(slot * 2, 0), 0);
            if (radius > style.a + 0.5)
                continue;
            float priority = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(slot * 2 + 1, 0), 0).r;
            if (winner == 0 || priority > winnerPriority)
            {
                winner = slot;
                winnerPriority = priority;
            }
        }
    }
    if (winner == 0)
        return display;
    return texelFetch(uTextures[pc.outlineStyleIndex], ivec2(winner * 2, 0), 0).rgb;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 scene = texelFetch(uTextures[pc.sceneIndex], pixel, 0);
    vec3 display = ApplyOutputTransform(scene.rgb, pc.exposure, pc.tonemapKnee, pc.tonemapEnabled != 0u);
    if (pc.outlineMaskIndex != kNoImage)
        display = CompositeOutline(display, pixel);
    outColor = vec4(kEncodeOutput ? EncodeSrgb(display) : display, scene.a);
}
