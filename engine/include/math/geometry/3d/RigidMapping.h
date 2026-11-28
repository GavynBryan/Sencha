#pragma once

#include <math/geometry/3d/Transform3d.h>

#include <cassert>
#include <cmath>

// How far from one each scale axis may be for the mapping to count as rigid.
inline constexpr float kRigidScaleTolerance = 1.0e-4f;
// How far below one the cosine between up and mapped up may fall for the mapping
// to count as keeping world up (about a quarter of a degree).
inline constexpr float kUprightCosineTolerance = 1.0e-5f;

[[nodiscard]] inline bool IsRigidMapping(const Transform3f& mapping)
{
    return std::abs(mapping.Scale.X - 1.0f) <= kRigidScaleTolerance
        && std::abs(mapping.Scale.Y - 1.0f) <= kRigidScaleTolerance
        && std::abs(mapping.Scale.Z - 1.0f) <= kRigidScaleTolerance;
}

[[nodiscard]] inline Transform3f InverseRigidMapping(const Transform3f& mapping)
{
    assert(IsRigidMapping(mapping) && "a scaled transform has no rigid inverse");
    const Quatf back = mapping.Rotation.Conjugate();
    return Transform3f{ back.RotateVector(-mapping.Position), back, Vec3d::One() };
}

[[nodiscard]] inline bool KeepsWorldUp(const Quatf& rotation)
{
    return 1.0f - rotation.RotateVector(Vec3d::Up()).Dot(Vec3d::Up()) <= kUprightCosineTolerance;
}

// Radians about +Y: 0 facing -Z, positive turning towards -X, as FromAxisAngle(Up, yaw) turns.
[[nodiscard]] inline float YawAboutWorldUp(const Quatf& rotation)
{
    const Vec3d forward = rotation.RotateVector(Vec3d::Forward());
    return std::atan2(-forward.X, -forward.Z);
}
