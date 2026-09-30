#pragma once

#include "brush/BrushMesh.h"
#include "brush/CarveStatus.h"

#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <vector>

// A face's plane as a 2D workspace: an orthonormal basis plus the face loop
// expressed in it. Every planar face operation works here rather than against
// world positions, so the operation never has to care whether the face is a
// quad, a triangle, an n-gon, or concave.
struct BrushFaceFrame
{
    Vec3d Origin;  // the face's first loop vertex
    Vec3d AxisU;   // in-plane, unit
    Vec3d AxisV;   // in-plane, unit; AxisU x AxisV == Normal
    Vec3d Normal;  // the face's outward normal, unit
    std::vector<Vec2d> Outline; // the loop in UV, counter-clockwise seen from outside

    [[nodiscard]] Vec2d ToFrame(Vec3d world) const;
    [[nodiscard]] Vec3d ToWorld(Vec2d uv) const;
};

struct BrushFaceFrameResult
{
    std::optional<BrushFaceFrame> Frame;
    CarveStatus Status = CarveStatus::Ok;
};

// The frame of `face`, or why it has none.
//
// The basis is canonical: derived from the plane and the world axes alone, so
// it is stable under any retopology and independent of material state. AxisV is
// world up projected into the plane, so anything authored with its rise along +V
// stands upright on any wall; AxisU follows from it. A face too close to
// horizontal has no up inside its own plane and falls back to the world axis
// least aligned with the normal. Either way an axis-aligned face lands on world
// axes, which is what makes grid snapping in the frame agree with the world grid.
//
// Deriving it from the loop instead would rotate the lattice every time a carve
// changed which edge came first, and deriving it from the material projection
// would rotate the lattice when someone rotated a texture.
//
// `planarTol` is relative: a vertex may sit off the plane by
// `planarTol * extent`, with an absolute floor so a small face is not held to
// an impossible standard. The caller owns the value, which is a cvar at the
// tool layer; the mesh layer does not read the console.
[[nodiscard]] BrushFaceFrameResult FaceFrame(const BrushMesh& mesh, std::uint32_t face, float planarTol);
