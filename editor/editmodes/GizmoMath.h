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
} // namespace GizmoMath
