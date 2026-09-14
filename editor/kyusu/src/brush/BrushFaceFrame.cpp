#include "brush/BrushFaceFrame.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// Absolute floor on the planarity budget, matching the carve's snap tolerance:
// below this, "off the plane" is indistinguishable from the rounding that
// snapping and welding already introduce.
constexpr float kPlanarFloor = 2e-4f;

// How much of world up has to survive projection into the face plane before the
// plane counts as having an up at all. A face within about a twentieth of a
// degree of horizontal falls back; below that the projected direction is mostly
// rounding noise and would not be stable between two coplanar faces.
constexpr float kUprightFloor = 1e-3f;

// The world axis least aligned with `normal`, ties broken in X, Y, Z order. Any
// such choice is guaranteed to have a component in the plane.
Vec3d LeastAlignedWorldAxis(Vec3d normal)
{
    const std::array<Vec3d, 3> axes = { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 } };
    int best = 0;
    float bestDot = std::abs(normal.Dot(axes[0]));
    for (int i = 1; i < 3; ++i)
    {
        const float d = std::abs(normal.Dot(axes[i]));
        if (d < bestDot)
        {
            bestDot = d;
            best = i;
        }
    }
    return axes[static_cast<std::size_t>(best)];
}
}

Vec2d BrushFaceFrame::ToFrame(Vec3d world) const
{
    const Vec3d d = world - Origin;
    return Vec2d{ d.Dot(AxisU), d.Dot(AxisV) };
}

Vec3d BrushFaceFrame::ToWorld(Vec2d uv) const
{
    return Origin + AxisU * uv.X + AxisV * uv.Y;
}

BrushFaceFrameResult FaceFrame(const BrushMesh& mesh, std::uint32_t face, float planarTol)
{
    if (face >= mesh.Faces.size())
        return { std::nullopt, CarveStatus::TopologyFailure };

    const BrushFace& f = mesh.Faces[face];
    if (f.Loop.size() < 3)
        return { std::nullopt, CarveStatus::TopologyFailure };

    const Vec3d normal = BrushComputeFaceNormal(mesh, f);
    if (normal.SqrMagnitude() < 0.5f)
        return { std::nullopt, CarveStatus::NonPlanarFace }; // Newell collapsed: no usable plane

    const Vec3d origin = mesh.Vertices[f.Loop[0]].Position;

    // Planarity is measured against the face's own size, so a large wall and a
    // small trim panel are held to the same relative flatness.
    float extent = 0.0f;
    for (std::uint32_t index : f.Loop)
        extent = std::max(extent, (mesh.Vertices[index].Position - origin).Magnitude());
    const float budget = std::max(planarTol * extent, kPlanarFloor);
    for (std::uint32_t index : f.Loop)
    {
        const float deviation = std::abs((mesh.Vertices[index].Position - origin).Dot(normal));
        if (deviation > budget)
            return { std::nullopt, CarveStatus::NonPlanarFace };
    }

    // AxisV is world up projected into the plane, so a shape authored with its
    // rise along +V stands upright on every wall. Reading the basis off the
    // least-aligned world axis instead put V on up for exactly one of the four
    // walls of a box; the opposite wall got it upside down and the other two got
    // it sideways. UvProjectionForNormal pins wall textures to up for the same
    // reason: two faces that disagree about which way is up rotate the same
    // authored thing as it turns a corner.
    Vec3d axisU;
    Vec3d axisV = Vec3d::Up() - normal * Vec3d::Up().Dot(normal);
    const float upright = axisV.Magnitude();
    if (upright > kUprightFloor)
    {
        axisV = axisV * (1.0f / upright);
        axisU = axisV.Cross(normal); // keeps AxisU x AxisV == Normal
    }
    else
    {
        // A face this close to horizontal has no up inside its own plane, so
        // there is nothing to stand upright against. The fallback promises
        // determinism and world alignment instead.
        const Vec3d seed = LeastAlignedWorldAxis(normal);
        axisU = seed - normal * seed.Dot(normal);
        const float length = axisU.Magnitude();
        if (length < 1e-6f)
            return { std::nullopt, CarveStatus::NonPlanarFace };
        axisU = axisU * (1.0f / length);
        axisV = normal.Cross(axisU);
    }

    BrushFaceFrame frame{ origin, axisU, axisV, normal, {} };
    frame.Outline.reserve(f.Loop.size());
    for (std::uint32_t index : f.Loop)
        frame.Outline.push_back(frame.ToFrame(mesh.Vertices[index].Position));

    return { std::move(frame), CarveStatus::Ok };
}
