#pragma once

#include <math/Mat.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

//=============================================================================
// Joint picking
//
// Where a posed skeleton's joints fall in a viewport, and which one a click
// meant. Pure over the model transforms and the camera, so the viewport draws
// the same markers the tests pick from.
//=============================================================================

struct AnimationJointMarker
{
    std::uint32_t Joint = 0;
    // Pixels from the viewport's top-left corner.
    float X = 0.0f;
    float Y = 0.0f;
    // Normalized depth; nearer is smaller.
    float Depth = 0.0f;
};

// Where one point falls in a viewport `width` by `height` pixels, in pixels
// from its top-left corner, with its normalized depth; nothing for a point
// behind the eye. What every overlay drawn over the viewport projects with.
struct AnimationViewportPoint
{
    float X = 0.0f;
    float Y = 0.0f;
    float Depth = 0.0f;
};
[[nodiscard]] std::optional<AnimationViewportPoint> ProjectAnimationViewportPoint(const Vec3d& point,
                                                                                  const Mat4& viewProjection,
                                                                                  float width, float height);

// Every joint in front of the camera, where `viewProjection` puts it in a
// viewport `width` by `height` pixels. Joints behind the eye are left out.
[[nodiscard]] std::vector<AnimationJointMarker> ProjectAnimationJoints(std::span<const Mat4> model,
                                                                       const Mat4& viewProjection, float width,
                                                                       float height);

// The joint whose marker is nearest (x, y) within `radius` pixels; of markers
// equally near, the one nearer the eye.
[[nodiscard]] std::optional<std::uint32_t> PickAnimationJoint(std::span<const AnimationJointMarker> markers, float x,
                                                              float y, float radius);
