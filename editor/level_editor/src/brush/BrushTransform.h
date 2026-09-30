#pragma once

#include <math/geometry/3d/Transform3d.h>

// world -> local: undo the transform's translation, rotation, then scale (zero
// scale components pass the coordinate through so the mapping stays total).
// The inverse of Transform3f::TransformPoint for the editor's TRS transforms;
// shared by the mesh-edit remaps, brush merge rebasing, and in-plane tools.
[[nodiscard]] inline Vec3d InverseTransformPoint(const Transform3f& t, Vec3d world)
{
    const Vec3d rel = world - t.Position;
    const Vec3d unrotated = t.Rotation.Conjugate().RotateVector(rel);
    return Vec3d(
        t.Scale.X != 0.0f ? unrotated.X / t.Scale.X : unrotated.X,
        t.Scale.Y != 0.0f ? unrotated.Y / t.Scale.Y : unrotated.Y,
        t.Scale.Z != 0.0f ? unrotated.Z / t.Scale.Z : unrotated.Z);
}
