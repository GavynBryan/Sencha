#pragma once

#include <math/geometry/3d/Transform3d.h>

#include <cmath>

// A rigid mapping is a Transform3f with unit scale: a rotation and a translation
// taking one space to another. These answer what such a mapping does to world up.

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
    const Quatf back = mapping.Rotation.Conjugate();
    return Transform3f{ back.RotateVector(-mapping.Position), back, Vec3d::One() };
}

[[nodiscard]] inline bool KeepsWorldUp(const Quatf& rotation)
{
    return 1.0f - rotation.RotateVector(Vec3d::Up()).Dot(Vec3d::Up()) <= kUprightCosineTolerance;
}

// The turn about world up that `rotation` gives forward, in radians, in the sense
// Quatf::FromAxisAngle(Vec3d::Up(), yaw) turns.
[[nodiscard]] inline float YawAboutWorldUp(const Quatf& rotation)
{
    const Vec3d forward = rotation.RotateVector(Vec3d::Forward());
    return std::atan2(-forward.X, -forward.Z);
}
