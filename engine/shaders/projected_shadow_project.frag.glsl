#version 450

// Projects the caster's silhouette tile onto this receiver fragment and
// writes the shadow amount into the shared screen mask. The pipeline blends
// with op MAX, so overlapping casters resolve to the union -- the strongest
// contribution -- instead of multiplying. Darkness is not applied here: the
// composite multiplies the scene by (1 - darkness * mask) exactly once.
layout(location = 0) in vec3 inWorldPos;

layout(set = 0, binding = 0) uniform ProjectedShadowFrame
{
    mat4 CameraViewProjection;
    mat4 ShadowViewProjection;
    vec4 TileScaleBias;
    vec4 Params; // x unused, y fade start, z silhouette bindless index
} frame;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) out float outAmount;

void main()
{
    // Orthographic shadow space: w is 1, xy in [-1,1], z already [0,1].
    vec4 shadowClip = frame.ShadowViewProjection * vec4(inWorldPos, 1.0);
    vec2 shadowUv = shadowClip.xy * 0.5 + 0.5;

    float inside = step(0.0, shadowUv.x) * step(shadowUv.x, 1.0)
                 * step(0.0, shadowUv.y) * step(shadowUv.y, 1.0)
                 * step(0.0, shadowClip.z) * step(shadowClip.z, 1.0);

    uint silhouette = uint(frame.Params.z + 0.5);
    float mask = texture(BindlessTextures[silhouette],
                         shadowUv * frame.TileScaleBias.xy + frame.TileScaleBias.zw).r;

    // Fade with depth along the projection so the shadow releases contact
    // smoothly instead of ending at a hard far plane.
    float fade = 1.0 - smoothstep(frame.Params.y, 1.0, shadowClip.z);

    outAmount = mask * fade * inside;
}
