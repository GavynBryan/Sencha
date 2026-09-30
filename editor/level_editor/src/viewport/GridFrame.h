#pragma once

#include "GridSettings.h"

#include <math/Quat.h>
#include <math/spatial/GridPlane.h>
#include <math/Vec.h>

#include <cmath>
#include <span>

// Pure construction of grid frames (origin + orthonormal in-plane axes) from
// scene geometry. Kept free of viewport/scene types so the math is unit-testable;
// EditorWorkspace resolves the geometry and applies the result to GridSettings.
namespace GridFrame
{
// The frame's right-handed orthonormal basis: U, N (= V x U, the plane normal;
// +Y for the default frame), V. The single definition every frame consumer
// (gizmo axes, ortho view mapping) shares.
inline void Basis(const GridSettings& settings, Vec3d& u, Vec3d& n, Vec3d& v)
{
    u = settings.AxisU;
    v = settings.AxisV;
    const Vec3d normal = v.Cross(u);
    n = normal.SqrMagnitude() > 1e-12f ? normal.Normalized() : Vec3d{ 0.0f, 1.0f, 0.0f };
}

// A world-frame vector re-expressed in the grid frame: the default frame is the
// identity, so (x, y, z) maps to x*U + y*N + z*V.
[[nodiscard]] inline Vec3d MapToFrame(Vec3d worldVec, Vec3d u, Vec3d n, Vec3d v)
{
    return u * worldVec.X + n * worldVec.Y + v * worldVec.Z;
}

// Frame aligned to a face: origin at the centroid, U along the reference edge
// direction (the face's longest edge reads most natural), V completing a
// right-handed basis in the face plane. Falls back to the current frame when
// the inputs are degenerate (zero normal, edge parallel to normal).
[[nodiscard]] inline bool FromFace(Vec3d centroid, Vec3d normal, Vec3d referenceEdgeDir,
                                   GridSettings& settings)
{
    constexpr float kEpsilon = 1e-6f;
    if (normal.SqrMagnitude() < kEpsilon || referenceEdgeDir.SqrMagnitude() < kEpsilon)
        return false;

    const Vec3d n = normal.Normalized();
    // Project the edge into the face plane so U lies exactly in it.
    Vec3d u = referenceEdgeDir - n * referenceEdgeDir.Dot(n);
    if (u.SqrMagnitude() < kEpsilon)
        return false;
    u = u.Normalized();

    settings.Origin = centroid;
    settings.AxisU = u;
    settings.AxisV = n.Cross(u).Normalized();
    return true;
}

// A snapping plane lying on a face: the face's own plane and in-plane axes,
// carrying the shared grid's spacing and snap toggle.
//
// `pointOnFace` only has to be some point the plane contains; it does not set
// the lattice phase. GridPlane::Snap rounds relative to its origin, so phasing
// the lattice on a point taken from the geometry -- a face's first loop vertex,
// say -- would make "snap to grid" mean "snap to that vertex" the moment the
// vertex is off the world lattice, which is what an earlier edit leaves behind.
[[nodiscard]] inline GridPlane SnapPlaneOnFace(Vec3d pointOnFace, Vec3d normal, Vec3d axisU,
                                               Vec3d axisV, const GridSettings& settings)
{
    const Vec3d n = normal.SqrMagnitude() > 1e-12f ? normal.Normalized() : Vec3d{ 0.0f, 1.0f, 0.0f };
    GridPlane plane;
    // The grid's own origin dropped onto the face's plane: the same lattice
    // phase, expressed at a point the plane actually contains.
    plane.Origin = settings.Origin - n * (settings.Origin - pointOnFace).Dot(n);
    plane.AxisU = axisU;
    plane.AxisV = axisV;
    plane.Spacing = settings.Spacing;
    plane.SnapEnabled = settings.SnapEnabled;
    return plane;
}

// The longest edge direction of a polygon loop (world-space corners, in order).
// Zero vector when the loop has fewer than two points.
[[nodiscard]] inline Vec3d LongestEdgeDirection(std::span<const Vec3d> corners)
{
    Vec3d best = {};
    float bestSqr = 0.0f;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const Vec3d edge = corners[(i + 1) % corners.size()] - corners[i];
        const float sqr = edge.SqrMagnitude();
        if (sqr > bestSqr)
        {
            bestSqr = sqr;
            best = edge;
        }
    }
    return best;
}

// Rotates the frame's axes about their own plane normal, re-orthonormalized so
// repeated rotations cannot drift.
inline void RotateInPlane(GridSettings& settings, float degrees)
{
    const Vec3d normal = settings.AxisU.Cross(settings.AxisV);
    if (normal.SqrMagnitude() < 1e-12f)
        return;
    const float radians = degrees * 3.14159265358979323846f / 180.0f;
    const Quatf rotation = Quatf::FromAxisAngle(normal.Normalized(), radians);
    const Vec3d u = rotation.RotateVector(settings.AxisU).Normalized();
    settings.AxisU = u;
    settings.AxisV = normal.Normalized().Cross(u).Normalized();
}
}
