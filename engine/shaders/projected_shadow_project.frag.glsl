#version 450

// Projects the caster's silhouette tile onto this receiver fragment and
// writes the shadow amount into the shared screen mask. The pipeline blends
// with op MAX, so overlapping casters resolve to the union -- the strongest
// contribution -- instead of multiplying. Darkness is not applied here: the
// composite multiplies the scene by (1 - darkness * mask) exactly once.
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;

layout(set = 0, binding = 0) uniform ProjectedShadowFrame
{
    mat4 CameraViewProjection;
    mat4 ShadowViewProjection;
    vec4 TileScaleBias;
    // x occluder index, y fade start, z silhouette index, w occlusion enable
    vec4 Params;
    vec4 DirectionBias; // xyz shadow direction, w normalized depth bias
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

    vec2 tileUv = shadowUv * frame.TileScaleBias.xy + frame.TileScaleBias.zw;
    uint silhouette = uint(frame.Params.z + 0.5);
    float mask = texture(BindlessTextures[silhouette], tileUv).r;

    // First-surface test: the shadow lands only on the nearest receiver
    // along the ray. A wall between the caster and a floor stores its depth
    // in the occluder tile, and the floor beyond fails this test instead of
    // catching the shadow through the wall. Constant bias from the world
    // -unit cvar (normalized per caster) plus an fwidth slope term against
    // grazing receivers.
    uint occluderSlot = uint(frame.Params.x + 0.5);
    float occluderZ = texture(BindlessTextures[occluderSlot], tileUv).r;
    float bias = frame.DirectionBias.w + 2.0 * fwidth(shadowClip.z);
    float occluded = mix(1.0, step(shadowClip.z, occluderZ + bias), frame.Params.w);

    // Fade with depth along the projection so the shadow releases contact
    // smoothly instead of ending at a hard far plane.
    float fade = 1.0 - smoothstep(frame.Params.y, 1.0, shadowClip.z);

    // A surface facing away from the shadow direction cannot receive -- the
    // back of a wall inside the volume stays clean. The shoulder fades
    // grazing surfaces instead of popping them.
    float facing = smoothstep(
        0.0, 0.2, -dot(normalize(inWorldNormal), frame.DirectionBias.xyz));

    outAmount = mask * fade * inside * facing * occluded;
}
