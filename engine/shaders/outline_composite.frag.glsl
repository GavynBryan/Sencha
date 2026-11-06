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
    // The projection's rows: clip z = a * z + b, clip w = c * z + d, for view z.
    vec4 depthTerms;
} pc;

// The mask's depth fraction, and how near something must stand to hide the ring:
// at least a few centimetres, so the floor beside a box's base does not hide its
// bottom edge, and more with distance, as depth precision falls off.
const float kOutlineDepthScale = 0.9375;
const float kHideBeyondMeters = 0.05;
const float kHideBeyondFraction = 0.02;

// A depth moved `meters` toward the camera.
float NearerDepth(float depth, float meters)
{
    float a = pc.depthTerms.x;
    float b = pc.depthTerms.y;
    float c = pc.depthTerms.z;
    float d = pc.depthTerms.w;
    float viewZ = (b - depth * d) / (depth * c - a);
    float nearer = viewZ - sign(viewZ) * max(meters, abs(viewZ) * kHideBeyondFraction);
    return clamp((a * nearer + b) / (c * nearer + d), 0.0, 1.0);
}

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
    float winnerDepth = 1.0;
    for (int ring = 1; ring <= 3; ++ring)
    {
        float radius = pc.outlineMaxWidth * float(ring) / 3.0;
        for (int i = 0; i < 8; ++i)
        {
            ivec2 tap = clamp(pixel + ivec2(round(kDirections[i] * radius)), ivec2(0), last);
            float texel = texelFetch(uTextures[pc.outlineMaskIndex], tap, 0).r;
            int slot = int(floor(texel));
            if (slot == 0)
                continue;
            float depth = fract(texel) / kOutlineDepthScale;
            vec4 style = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(slot * 2, 0), 0);
            if (radius > style.a + 0.5)
                continue;
            float priority = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(slot * 2 + 1, 0), 0).r;
            if (winner == 0 || priority > winnerPriority)
            {
                winner = slot;
                winnerPriority = priority;
                winnerDepth = depth;
            }
            else if (slot == winner)
            {
                winnerDepth = min(winnerDepth, depth);
            }
        }
    }
    if (winner == 0)
        discard;
    // The object's own depth, moved toward the camera: anything nearer than that
    // stands in front of the object here, and the depth test hides the ring on it.
    gl_FragDepth = NearerDepth(winnerDepth, kHideBeyondMeters);
    vec3 color = texelFetch(uTextures[pc.outlineStyleIndex], ivec2(winner * 2, 0), 0).rgb;
    outColor = vec4(kEncodeOutput ? EncodeSrgb(color) : color, 1.0);
}
