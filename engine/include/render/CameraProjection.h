#pragma once

#include <math/Mat.h>
#include <math/Quat.h>
#include <math/Vec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

//=============================================================================
// Vulkan clip-space projections
//
// The one definition of the renderer's clip-space convention, shared by the
// game camera, the editor viewports, and the material preview. Every projection
// that feeds a Vulkan pipeline comes from here.
//
// The convention, recorded in docs/renderer/vulkan-backend.md:
//
//   - Y is flipped in the projection rather than by a negative-height viewport,
//     because Vulkan NDC has +Y pointing down.
//   - Depth is standard [0,1], near at 0 and far at 1. NOT reversed Z. Every
//     pipeline pairs this with VK_COMPARE_OP_LESS_OR_EQUAL and a 1.0 depth
//     clear; changing it here without changing those is a silently wrong
//     depth test, not a compile error.
//
// Mat4::MakePerspective in math/Mat.h is the OpenGL-convention helper for
// non-Vulkan callers ([-1,1] depth, no Y flip). It is not a fourth copy of
// this and the two are not interchangeable.
//=============================================================================

[[nodiscard]] inline Mat4 MakeVulkanPerspective(float fovYRadians,
                                                float aspect,
                                                float nearPlane,
                                                float farPlane)
{
    const float tanHalfFov = std::tan(fovYRadians * 0.5f);
    Mat4 result;
    result[0][0] = 1.0f / (aspect * tanHalfFov);
    result[1][1] = -1.0f / tanHalfFov;
    result[2][2] = farPlane / (nearPlane - farPlane);
    result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    result[3][2] = -1.0f;
    return result;
}

[[nodiscard]] inline Mat4 MakeVulkanOrthographic(float left,
                                                 float right,
                                                 float bottom,
                                                 float top,
                                                 float nearPlane,
                                                 float farPlane)
{
    Mat4 result = Mat4::Identity();
    result[0][0] = 2.0f / (right - left);
    result[1][1] = -2.0f / (top - bottom);
    result[2][2] = 1.0f / (nearPlane - farPlane);
    result[0][3] = -(right + left) / (right - left);
    // Positive, unlike the X term: the Y flip negates the whole row, scale and
    // translation together. Every caller so far passes a volume centred on Y,
    // where this term is zero and the sign cannot be observed -- an off-centre
    // ortho view (a panned viewport, a shadow cascade) is what reveals it.
    result[1][3] = (top + bottom) / (top - bottom);
    result[2][3] = nearPlane / (nearPlane - farPlane);
    return result;
}

// Where a world-space direction lands in a `width` by `height` pixel viewport,
// origin top left, seen from a view facing `rotation` through a vertical field of
// view: the projection a perspective camera draws with, taken to infinity, so
// the view's position and its near and far planes play no part. Nullopt for a
// direction at or behind the view's plane. What lays a reticle or a marker on
// a frame where the frame's own camera would draw it.
[[nodiscard]] inline std::optional<Vec2d> ProjectDirectionToViewport(const Quatf& rotation,
                                                                     float fovYRadians,
                                                                     float width,
                                                                     float height,
                                                                     const Vec3d& direction)
{
    const Vec3d local = rotation.Inverse().RotateVector(direction);
    const Mat4 projection = MakeVulkanPerspective(fovYRadians, width / height, 1.0f, 2.0f);
    // A direction is a point at infinity: w is zero, so depth never enters.
    const Vec4 clip = projection * Vec4{ local.X, local.Y, local.Z, 0.0f };
    if (!(clip.W > 1.0e-6f))
        return std::nullopt;
    return Vec2d{ (clip.X / clip.W + 1.0f) * 0.5f * width, (clip.Y / clip.W + 1.0f) * 0.5f * height };
}

// A rectangle of the frame in the caller's own units, origin top left: the
// pixels a viewport tile covers, or the layout units a HUD surface lays out in.
// It must show the whole view the projection describes, so it has that view's
// aspect; a HUD over the full window passes the window's size.
struct ViewportRect
{
    Vec2d Offset;
    Vec2d Size;
};

enum class ViewportPlacement : std::uint8_t
{
    // In front of the camera, inside the rectangle and between the near and far
    // planes: where the frame draws the point.
    Visible,
    // In front of the camera but not drawn: outside the rectangle, nearer than
    // the near plane, or past the far one.
    OutsideViewport,
    // On or behind the plane the camera looks out of.
    BehindCamera,
};

struct ViewportProjection
{
    ViewportPlacement Placement = ViewportPlacement::BehindCamera;
    // Where the point lands, in the rectangle's units with its offset applied,
    // and as a fraction of it ([0, 1] across a visible point). Outside the
    // rectangle for a point outside it. Zero behind the camera, where the
    // projection mirrors and names no place on the frame.
    Vec2d Position;
    Vec2d Normalized;
    // How far in front of the camera's plane the point is, in world units.
    // Zero or negative behind it.
    float ViewDepth = 0.0f;
    // Unit direction, in the rectangle's units, from its centre toward the point:
    // where an edge marker for it would sit. Behind the camera it is the way to
    // turn toward the point, not the mirrored projection. Zero for a point on
    // the view's axis.
    Vec2d EdgeDirection;
};

// Where a world-space point lands in `viewport`, seen through the `view` and
// `projection` a frame was drawn with. The one definition of world-to-overlay
// placement: perspective and orthographic alike, in the clip-space convention
// every pipeline uses, so a marker laid here sits on the pixels the point was
// drawn on. Pass the presented camera's matrices (camera/PresentedFrame.h), not
// ones built from the camera entity, or shake and field-of-view effects pull
// the marker off the picture.
[[nodiscard]] inline ViewportProjection ProjectWorldPointToViewport(const Mat4& view,
                                                                    const Mat4& projection,
                                                                    const ViewportRect& viewport,
                                                                    const Vec3d& point)
{
    const Vec4 inView = view * Vec4{ point.X, point.Y, point.Z, 1.0f };
    const Vec4 clip = projection * inView;

    ViewportProjection result;
    // Cameras look down -Z in view space; MakeVulkanPerspective's w is -z.
    result.ViewDepth = -inView.Z;

    // Through |w|, so that behind a perspective camera the direction is the
    // true one: dividing by the negative w is what mirrors the projection.
    const float w = std::max(std::abs(clip.W), 1.0e-6f);
    const Vec2d ndc{ clip.X / w, clip.Y / w };
    const Vec2d onScreen{ ndc.X * viewport.Size.X, ndc.Y * viewport.Size.Y };
    const float length = onScreen.Magnitude();
    if (length > 1.0e-6f)
        result.EdgeDirection = onScreen / length;

    if (!(result.ViewDepth > 0.0f))
        return result;

    const float depth = clip.Z / w;
    result.Normalized = Vec2d{ (ndc.X + 1.0f) * 0.5f, (ndc.Y + 1.0f) * 0.5f };
    result.Position = Vec2d{ viewport.Offset.X + result.Normalized.X * viewport.Size.X,
                             viewport.Offset.Y + result.Normalized.Y * viewport.Size.Y };
    const bool inside = ndc.X >= -1.0f && ndc.X <= 1.0f && ndc.Y >= -1.0f && ndc.Y <= 1.0f
                     && depth >= 0.0f && depth <= 1.0f;
    result.Placement = inside ? ViewportPlacement::Visible : ViewportPlacement::OutsideViewport;
    return result;
}

// Maps a clip-space position back to the world-space *direction* it was seen
// along, for anything shaded by where the eye is looking rather than by where a
// surface is: the background gradient today, a cubemap or an atmosphere later.
//
// The view matrix's translation is dropped before inverting, so what comes back
// out is a direction rather than a point. Keeping the translation would make
// the result move with the camera, which for a background means the sky slides
// as the player walks.
//
// The Y flip lives in the projection (see above), so it inverts along with it:
// clip y = -1 is the top of the screen and must reconstruct to a direction
// above the horizon. Nothing else in the pipeline would catch that being
// backwards.
[[nodiscard]] inline Mat4 MakeInverseSkyViewProjection(const Mat4& view,
                                                       const Mat4& projection)
{
    Mat4 rotationOnly = view;
    rotationOnly[0][3] = 0.0f;
    rotationOnly[1][3] = 0.0f;
    rotationOnly[2][3] = 0.0f;
    return (projection * rotationOnly).Inverse();
}
