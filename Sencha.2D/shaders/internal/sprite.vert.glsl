#version 450
#extension GL_GOOGLE_include_directive : require

#include "sencha/frame_ubo.glsli"

// Per-instance vertex inputs (VK_VERTEX_INPUT_RATE_INSTANCE).
// Layout must match SpriteFeature::GpuInstance (48 bytes).
layout(location = 0) in vec2  iCenter;       // screen-pixel centre, origin top-left
layout(location = 1) in vec2  iHalfExtents;  // half-size in pixels
layout(location = 2) in vec2  iUvMin;        // UV rect on the source texture
layout(location = 3) in vec2  iUvMax;
layout(location = 4) in uint  iColor;        // packed rgba8 tint
layout(location = 5) in uint  iTextureIndex; // bindless slot
layout(location = 6) in vec2  iSinCos;       // precomputed sin/cos of rotation

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uint vTextureIndex;

void main()
{
    // Six vertices / two triangles generated from gl_VertexIndex.
    // No index buffer: the sprite quad is fully index-free.
    const vec2 kCorner[6] = vec2[](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
        vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );
    const vec2 kCornerUv[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );

    // Rotate the local corner offset around the sprite centre.
    vec2 local   = kCorner[gl_VertexIndex] * iHalfExtents;
    float s      = iSinCos.x;
    float c      = iSinCos.y;
    vec2 rotated = vec2(local.x * c - local.y * s,
                        local.x * s + local.y * c);
    vec2 pixels  = iCenter + rotated;

    // Screen-pixel to NDC.  Origin is top-left so Y is negated.
    vec2 ndc  = pixels * uFrame.InvViewport * 2.0 - 1.0;
    ndc.y     = -ndc.y;

    gl_Position  = vec4(ndc, 0.0, 1.0);
    vUv          = mix(iUvMin, iUvMax, kCornerUv[gl_VertexIndex]);
    vColor       = unpackUnorm4x8(iColor);
    vTextureIndex = iTextureIndex;
}
