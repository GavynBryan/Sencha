#pragma once

#include "brush/BrushTransform.h"

#include <math/geometry/3d/Plane.h>
#include <math/geometry/3d/Transform3d.h>
#include <math/Vec.h>

#include <cmath>
#include <optional>

// The plane a clip line stands for, and how it is carried into a brush's own
// space. Pure math over engine types, so every rule that decides where a cut
// lands is unit-tested without a viewport.
//
// "Front" is the side the returned normal points to. The drawing order sets it:
// drawing the same line the other way swaps the halves, which is the flip
// control, and the only one.
namespace ClipPlaneMath
{
// Below this the line has no direction to stand a plane on.
inline constexpr float kMinimumLength = 1e-4f;

// An orthographic view: the plane containing the line and the view direction,
// so it is perpendicular to the screen. Nullopt for a line too short, or one
// running along the view.
[[nodiscard]] inline std::optional<Plane> ThroughLineAlongView(Vec3d a, Vec3d b, Vec3d view)
{
    const Vec3d normal = (b - a).Cross(view);
    if ((b - a).Magnitude() < kMinimumLength || normal.Magnitude() < kMinimumLength)
        return std::nullopt;
    return Plane::FromNormalAndPoint(normal.Normalized(), a);
}

// A perspective view: the plane through the eye and both points, which is the
// plane the drawn line projects to. Nullopt for a line too short, or one whose
// points sight along a single ray from the eye.
[[nodiscard]] inline std::optional<Plane> ThroughLineAndEye(Vec3d a, Vec3d b, Vec3d eye)
{
    const Vec3d normal = (a - eye).Cross(b - eye);
    if ((b - a).Magnitude() < kMinimumLength || normal.Magnitude() < kMinimumLength)
        return std::nullopt;
    return Plane::FromNormalAndPoint(normal.Normalized(), a);
}

// The world plane in a brush's local space. Three points of the plane are
// carried through the inverse transform and the plane rebuilt from them, which
// is exact under non-uniform scale where transforming the normal is not; then
// the sign is fixed against a known front point, because a mirrored transform
// (negative determinant) flips the rebuilt normal and world Front has to stay
// Front. `front` is any world point on the plane's positive side.
[[nodiscard]] inline Plane InLocal(const Plane& world, Vec3d front, const Transform3f& transform)
{
    const Plane plane = world.Normalized();
    const Vec3d origin = plane.Normal * -plane.D; // a point on the plane
    // Two in-plane directions from the axis least aligned with the normal.
    const Vec3d axis = std::abs(plane.Normal.X) < 0.9f ? Vec3d{ 1, 0, 0 } : Vec3d{ 0, 1, 0 };
    const Vec3d u = plane.Normal.Cross(axis).Normalized();
    const Vec3d v = plane.Normal.Cross(u).Normalized();

    const Vec3d p0 = InverseTransformPoint(transform, origin);
    const Vec3d p1 = InverseTransformPoint(transform, origin + u);
    const Vec3d p2 = InverseTransformPoint(transform, origin + v);
    Vec3d normal = (p1 - p0).Cross(p2 - p0).Normalized();
    Plane local = Plane::FromNormalAndPoint(normal, p0);
    if (local.SignedDistanceTo(InverseTransformPoint(transform, front)) < 0.0f)
        local = Plane::FromNormalAndPoint(normal * -1.0f, p0);
    return local;
}
}
