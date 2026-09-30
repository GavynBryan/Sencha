#version 450

// Screen-space line expansion, instanced: one instance per segment, six
// vertices per instance forming the ribbon quad. This vertex picks its
// endpoint and quad side from gl_VertexIndex, then offsets along the
// segment's screen-space perpendicular by a fixed pixel amount, so the stroke
// has a constant pixel width under perspective. Depth (z, w) is kept from the
// endpoint, so the quad still depth-tests like the line.

layout(push_constant) uniform PushConstants {
    mat4 ViewProjection;
    vec2 ViewportPixels;   // viewport size in pixels, for the pixel<->NDC conversion
} Push;

layout(location = 0) in vec3 InA;       // segment endpoint A (per instance)
layout(location = 1) in vec3 InB;       // segment endpoint B (per instance)
layout(location = 2) in vec4 InColor;
layout(location = 3) in float InWidthPx; // full stroke width

layout(location = 0) out vec4 OutColor;
layout(location = 1) out float OutAcrossPx;     // signed pixel distance from center
layout(location = 2) out float OutHalfWidthPx;

// Two triangles tiling the ribbon quad: (A-, A+, B-) and (B-, A+, B+), where
// -/+ is the perpendicular side. A+ and B- are opposite corners, so the
// triangles share that diagonal and tile cleanly (and the AA coordinate runs
// consistently across the quad).
const int   kEndpoint[6] = int[](0, 0, 1, 1, 0, 1);
const float kSide[6]     = float[](-1.0, 1.0, -1.0, -1.0, 1.0, 1.0);

// Extra geometry past the stroke edge so the fragment feather has room to fade out.
const float kFeatherPx = 1.0;

void main()
{
    const bool atA = kEndpoint[gl_VertexIndex] == 0;
    const vec3 thisPos  = atA ? InA : InB;
    const vec3 otherPos = atA ? InB : InA;
    const float side = kSide[gl_VertexIndex];
    const float halfWidthPx = InWidthPx * 0.5;

    vec4 clipThis  = Push.ViewProjection * vec4(thisPos, 1.0);
    vec4 clipOther = Push.ViewProjection * vec4(otherPos, 1.0);

    vec2 ndcThis  = clipThis.xy  / clipThis.w;
    vec2 ndcOther = clipOther.xy / clipOther.w;

    // Segment direction in pixel space, then its perpendicular. `this - other` points
    // opposite ways at the two endpoints, which would flip the perpendicular (and so the
    // AA coordinate and quad side) between them, collapsing the centerline onto a
    // diagonal. Canonicalize the direction so both endpoints agree on one perpendicular.
    vec2 halfPx = Push.ViewportPixels * 0.5;
    vec2 dirPx = (ndcThis - ndcOther) * halfPx;
    if (dirPx.x < 0.0 || (dirPx.x == 0.0 && dirPx.y < 0.0))
        dirPx = -dirPx;
    float len = length(dirPx);
    dirPx = len > 1e-5 ? dirPx / len : vec2(1.0, 0.0);
    vec2 perp = vec2(-dirPx.y, dirPx.x);

    float extentPx = halfWidthPx + kFeatherPx;
    vec2 offsetNdc = (perp * (side * extentPx)) / halfPx;

    vec2 ndcOut = ndcThis + offsetNdc;
    gl_Position = vec4(ndcOut * clipThis.w, clipThis.z, clipThis.w);

    OutColor = InColor;
    OutAcrossPx = side * extentPx;
    OutHalfWidthPx = halfWidthPx;
}
