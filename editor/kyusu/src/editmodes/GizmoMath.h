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
