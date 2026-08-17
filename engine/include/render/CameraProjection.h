#pragma once

#include <math/Mat.h>

#include <cmath>

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
