#version 450

// Screen-space line expansion. Each segment is six vertices (a quad); this vertex
// offsets its endpoint along the segment's screen-space perpendicular by a fixed
// pixel amount, so the stroke has a constant pixel width under perspective. Depth
// (z, w) is kept from the endpoint, so the quad still depth-tests like the line.

layout(push_constant) uniform PushConstants {
    mat4 ViewProjection;
    vec2 ViewportPixels;   // viewport size in pixels, for the pixel<->NDC conversion
} Push;

layout(location = 0) in vec3 InPosition;     // this endpoint
layout(location = 1) in vec3 InOther;        // opposite endpoint
layout(location = 2) in vec4 InColor;
layout(location = 3) in float InHalfWidthPx; // half stroke width
layout(location = 4) in float InSide;        // -1 or +1: which edge of the quad

layout(location = 0) out vec4 OutColor;
layout(location = 1) out float OutAcrossPx;     // signed pixel distance from center
layout(location = 2) out float OutHalfWidthPx;

// Extra geometry past the stroke edge so the fragment feather has room to fade out.
const float kFeatherPx = 1.0;

void main()
{
    vec4 clipThis  = Push.ViewProjection * vec4(InPosition, 1.0);
    vec4 clipOther = Push.ViewProjection * vec4(InOther, 1.0);

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

    float extentPx = InHalfWidthPx + kFeatherPx;
    vec2 offsetNdc = (perp * (InSide * extentPx)) / halfPx;

    vec2 ndcOut = ndcThis + offsetNdc;
    gl_Position = vec4(ndcOut * clipThis.w, clipThis.z, clipThis.w);

    OutColor = InColor;
    OutAcrossPx = InSide * extentPx;
    OutHalfWidthPx = InHalfWidthPx;
}
