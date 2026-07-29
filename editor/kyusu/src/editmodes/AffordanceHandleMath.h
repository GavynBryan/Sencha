#pragma once

#include <math/geometry/2d/Aabb2d.h>
#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace AffordanceHandleMath
{
inline constexpr double MinimumThickness = 0.05;

inline double Axis(const Vec3d& value, int axis)
{
    return axis == 0 ? value.X : axis == 1 ? value.Y : value.Z;
}

inline void SetAxis(Vec3d& value, int axis, double coordinate)
{
    (axis == 0 ? value.X : axis == 1 ? value.Y : value.Z) =
        static_cast<float>(coordinate);
}

[[nodiscard]] inline std::optional<Aabb3d> ResizeAabbFace(
    Aabb3d value, int axis, bool maximum, double coordinate)
{
    if (axis < 0 || axis > 2)
        return std::nullopt;
    if (maximum)
    {
        double bounded = std::max(coordinate,
                                  Axis(value.Min, axis) + MinimumThickness);
        SetAxis(value.Max, axis, bounded);
    }
    else
    {
        double bounded = std::min(coordinate,
                                  Axis(value.Max, axis) - MinimumThickness);
        SetAxis(value.Min, axis, bounded);
    }
    return value.IsValid() ? std::optional{ value } : std::nullopt;
}

// Returns the edited local rectangle bounds. Unlike changing only a half
// extent, editing one edge changes the bounds' center and keeps the opposite
// edge fixed.
[[nodiscard]] inline std::optional<Aabb2d> ResizeRectangleEdge(
    Vec2d halfExtents, int axis, bool maximum, double coordinate)
{
    if (axis < 0 || axis > 1)
        return std::nullopt;
    Aabb2d bounds = Aabb2d::FromMinMax(
        { -halfExtents.X, -halfExtents.Y }, halfExtents);
    float& edited = axis == 0
        ? (maximum ? bounds.Max.X : bounds.Min.X)
        : (maximum ? bounds.Max.Y : bounds.Min.Y);
    const double opposite = axis == 0
        ? (maximum ? bounds.Min.X : bounds.Max.X)
        : (maximum ? bounds.Min.Y : bounds.Max.Y);
    edited = static_cast<float>(maximum
        ? std::max(coordinate, opposite + MinimumThickness)
        : std::min(coordinate, opposite - MinimumThickness));
    return bounds.IsValid() ? std::optional{ bounds } : std::nullopt;
}
} // namespace AffordanceHandleMath
