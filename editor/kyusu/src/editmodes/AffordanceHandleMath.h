#pragma once

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
    Aabb3d value, int axis, bool maximum, double coordinate,
    std::optional<float> minimumLimit = {},
    std::optional<float> maximumLimit = {})
{
    if (axis < 0 || axis > 2)
        return std::nullopt;
    if (maximum)
    {
        double bounded = std::max(coordinate,
                                  Axis(value.Min, axis) + MinimumThickness);
        if (maximumLimit)
            bounded = std::min(bounded, static_cast<double>(*maximumLimit));
        SetAxis(value.Max, axis, bounded);
    }
    else
    {
        double bounded = std::min(coordinate,
                                  Axis(value.Max, axis) - MinimumThickness);
        if (minimumLimit)
            bounded = std::max(bounded, static_cast<double>(*minimumLimit));
        SetAxis(value.Min, axis, bounded);
    }
    return value.IsValid() ? std::optional{ value } : std::nullopt;
}

[[nodiscard]] inline std::optional<Vec2d> ResizeRectangleEdge(
    Vec2d halfExtents, int axis, double signedCoordinate)
{
    if (axis < 0 || axis > 1)
        return std::nullopt;
    (axis == 0 ? halfExtents.X : halfExtents.Y) = static_cast<float>(
        std::max(MinimumThickness * 0.5, std::abs(signedCoordinate)));
    return halfExtents;
}
} // namespace AffordanceHandleMath
