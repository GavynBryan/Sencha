#pragma once

#include <math/Vec.h>
#include <math/geometry/3d/Ray3d.h>

#include <cmath>
#include <optional>

// Pure gizmo-drag math — no GUI, no scene. Isolated and header-only so the
// closest-point and snap logic (where the backwards-drag and relative-vs-absolute
// snap bugs lived) is unit-tested directly.
// (docs/architecture/hardening-and-consolidation.md W2)
namespace GizmoMath
{
inline constexpr double kParallelEpsilon = 1.0e-8;

// Parameter s along the axis line (pivot + s*axisDir) of the point closest to the
// camera ray. nullopt when axis and ray are near-parallel (ill-conditioned).
// w0 runs pivot->origin so s carries the axis's own sign: a positive drag along
// +axis yields positive s. (The opposite convention inverts the drag — the bug.)
[[nodiscard]] inline std::optional<double> ClosestAxisParam(Vec3d pivot, Vec3d axisDir, const Ray3d& ray)
{
    const Vec3d w0 = pivot - ray.Origin;
    const double b = axisDir.Dot(ray.Direction);
    const double denom = 1.0 - b * b;
    if (std::abs(denom) < kParallelEpsilon)
        return std::nullopt;

    const double d = axisDir.Dot(w0);
    const double e = ray.Direction.Dot(w0);
    return (b * e - d) / denom;
}

// Intersection of the ray with the plane through `point` with `normal`.
// nullopt when the ray is near-parallel to the plane or the hit is behind the
// ray origin. Drives the gizmo's center (view-plane) drag.
[[nodiscard]] inline std::optional<Vec3d> RayPlanePoint(const Ray3d& ray, Vec3d point, Vec3d normal)
{
    const double denom = ray.Direction.Dot(normal);
    if (std::abs(denom) < kParallelEpsilon)
        return std::nullopt;
    const double t = (point - ray.Origin).Dot(normal) / denom;
    if (t < 0.0)
        return std::nullopt;
    return ray.Origin + ray.Direction * static_cast<float>(t);
}

// The angle at which the cursor ray crosses the plane through `centre` with
// `normal`, measured from +u toward +v. nullopt when the ray is near-parallel to
// the plane and there is no crossing to speak of.
//
// Unlike RayPlanePoint this keeps a crossing behind the ray origin. A ring or a
// dial is something already on screen and already being dragged; refusing the
// grazing case would drop the gesture rather than improve it.
[[nodiscard]] inline std::optional<double> AngleOnPlane(const Ray3d& ray, Vec3d centre, Vec3d normal,
                                                        Vec3d u, Vec3d v)
{
    const double denom = ray.Direction.Dot(normal);
    if (std::abs(denom) < kParallelEpsilon)
        return std::nullopt;
    const double t = (centre - ray.Origin).Dot(normal) / denom;
    const Vec3d relative = ray.Origin + ray.Direction * static_cast<float>(t) - centre;
    return std::atan2(static_cast<double>(relative.Dot(v)), static_cast<double>(relative.Dot(u)));
}

// `current` minus `previous`, brought into (-pi, pi], so a drag past a half turn
// keeps turning the same way instead of flipping to the short way round.
[[nodiscard]] inline double UnwrapAngleDelta(double current, double previous)
{
    constexpr double kPi = 3.14159265358979323846;
    double delta = current - previous;
    if (delta > kPi)
        delta -= 2.0 * kPi;
    if (delta < -kPi)
        delta += 2.0 * kPi;
    return delta;
}

// The nearest multiple of `increment`. An increment of zero or less is the
// identity, which is how free rotation is spelled.
[[nodiscard]] inline double SnapAngle(double radians, double increment)
{
    if (increment <= 0.0)
        return radians;
    return std::round(radians / increment) * increment;
}

// Absolute snap: the offset that lands the pivot on the nearest grid line along
// the axis (measured from the grid origin), so geometry snaps to grid positions,
// not just to grid-sized steps. spacing <= 0 disables snapping.
[[nodiscard]] inline double SnapAxisOffset(double rawOffset, double pivotCoord, double originCoord, float spacing)
{
    if (spacing <= 0.0f)
        return rawOffset;
    const double target = pivotCoord + rawOffset;
    const double snapped = originCoord + std::round((target - originCoord) / spacing) * spacing;
    return snapped - pivotCoord;
}

// True when coord sits on the grid lattice (originCoord + k*spacing) within
// tol. spacing <= 0 has no lattice.
[[nodiscard]] inline bool OnGridLattice(double coord, double originCoord, float spacing, double tol)
{
    if (spacing <= 0.0f)
        return false;
    const double rel = coord - originCoord;
    const double nearest = std::round(rel / spacing) * spacing;
    return std::abs(rel - nearest) <= tol;
}

// Absolute snap for an axis scale: adjust the factor so the driven AABB bound
// (which factor f places at pivot + (bound - pivot) * f) lands on the nearest
// grid line, so scaled geometry faces sit on grid positions rather than the
// factor snapping to arbitrary increments. spacing <= 0 or a degenerate extent
// passes the raw factor through. The result can be <= 0 when the nearest line
// crosses the pivot; the caller clamps to its minimum factor.
[[nodiscard]] inline double SnapScaleFactor(double rawFactor, double pivotCoord, double boundCoord,
                                            double originCoord, float spacing)
{
    const double extent = boundCoord - pivotCoord;
    if (spacing <= 0.0f || std::abs(extent) < kParallelEpsilon)
        return rawFactor;
    const double target = pivotCoord + extent * rawFactor;
    const double snapped = originCoord + std::round((target - originCoord) / spacing) * spacing;
    return (snapped - pivotCoord) / extent;
}
} // namespace GizmoMath
