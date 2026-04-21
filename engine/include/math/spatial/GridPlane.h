#pragma once

#include <math/Vec.h>

#include <cmath>
#include <cstdint>

struct GridPlane
{
    Vec3d Origin = {};
    Vec3d AxisU = { 1, 0, 0 };
    Vec3d AxisV = { 0, 0, 1 };
    float Spacing = 1.0f;
    uint32_t Subdivisions = 10;

    Vec3d Snap(Vec3d worldPos) const
    {
        const Vec3d projected = Project(worldPos);
        const Vec3d local = projected - Origin;
        const float snappedU = std::round(local.Dot(AxisU) / Spacing) * Spacing;
        const float snappedV = std::round(local.Dot(AxisV) / Spacing) * Spacing;
        return Origin + AxisU * snappedU + AxisV * snappedV;
    }

    Vec3d Project(Vec3d worldPos) const
    {
        const Vec3d local = worldPos - Origin;
        const float u = local.Dot(AxisU);
        const float v = local.Dot(AxisV);
        return Origin + AxisU * u + AxisV * v;
    }
};

namespace GridPlanes
{
inline GridPlane XZ(float spacing = 1.0f)
{
    GridPlane plane;
    plane.AxisU = { 1, 0, 0 };
    plane.AxisV = { 0, 0, 1 };
    plane.Spacing = spacing;
    return plane;
}

inline GridPlane XY(float spacing = 1.0f)
{
    GridPlane plane;
    plane.AxisU = { 1, 0, 0 };
    plane.AxisV = { 0, 1, 0 };
    plane.Spacing = spacing;
    return plane;
}

inline GridPlane YZ(float spacing = 1.0f)
{
    GridPlane plane;
    plane.AxisU = { 0, 1, 0 };
    plane.AxisV = { 0, 0, 1 };
    plane.Spacing = spacing;
    return plane;
}
}
