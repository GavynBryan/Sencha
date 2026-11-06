#version 450

// Draws the outer outline over the presented frame, scissored to the highlighted
// objects' screen bounds, so its cost follows what is highlighted rather than the
// window. Only on pixels no highlighted surface covers, so a highlighted object's
// material is never painted over. A fixed ring of taps at three radii up to the widest
// style; a style counts only within its own width, and the highest priority takes a
// pixel two styles compete for. Mirrored by OutlineCompositePush in OutlineCompositePass.cpp.

#include "display_encode.glsli"

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kEncodeOutput = false;

layout(push_constant) uniform OutlineCompositePC {
    uint outlineMaskIndex;
    uint outlineStyleIndex;
    uint outlineTilesIndex;
    float outlineMaxWidth;
} pc;

const int kTileSize = 8;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // Most of the scissor is far from any outline; one fetch says so.
    if (texelFetch(uTextures[pc.outlineTilesIndex], pixel / kTileSize, 0).r == 0.0)
        discard;
    if (texelFetch(uTextures[pc.outlineMaskIndex], pixel, 0).r != 0.0)
        discard;

    ivec2 last = textureSize(uTextures[pc.outlineMaskIndex], 0) - ivec2(1);
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
        discard;
    vec3 color = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(winner * 2, 0), 0).rgb;
    outColor = vec4(kEncodeOutput ? EncodeSrgb(color) : color, 1.0);
}
