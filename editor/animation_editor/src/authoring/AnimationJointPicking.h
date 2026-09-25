#pragma once

#include <math/Mat.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct AnimationJointMarker
{
    std::uint32_t Joint = 0;
    // Pixels from the viewport's top-left corner.
    float X = 0.0f;
    float Y = 0.0f;
    // Normalized depth; nearer is smaller.
    float Depth = 0.0f;
};

// Same units as AnimationJointMarker.
struct AnimationViewportPoint
{
    float X = 0.0f;
    float Y = 0.0f;
    float Depth = 0.0f;
};
// Empty for a point behind the eye.
[[nodiscard]] std::optional<AnimationViewportPoint> ProjectAnimationViewportPoint(const Vec3d& point,
                                                                                  const Mat4& viewProjection,
                                                                                  float width, float height);

// Joints behind the eye are left out.
[[nodiscard]] std::vector<AnimationJointMarker> ProjectAnimationJoints(std::span<const Mat4> model,
                                                                       const Mat4& viewProjection, float width,
                                                                       float height);

// Nearest marker within `radius` pixels; ties go to the one nearer the eye.
[[nodiscard]] std::optional<std::uint32_t> PickAnimationJoint(std::span<const AnimationJointMarker> markers, float x,
                                                              float y, float radius);
