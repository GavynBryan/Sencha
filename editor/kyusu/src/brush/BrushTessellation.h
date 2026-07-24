#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <functional>
#include <span>

//=============================================================================
// BrushTessellation — the one place a brush turns into renderable/cookable
// triangles. Pure geometry: fan-triangulates each face, bakes world-space
// positions + normals and projected UVs. (04- S5)
//
// Both the editor's solid preview and the level cook (05-) consume this, so the
// previewed geometry and the cooked mesh can never disagree — they are the same
// triangles. UVs are evaluated from each face's projection in LOCAL space (hence
// resize-invariant); positions/normals are transformed to world.
//
// Faces are assumed planar and simple (non-self-intersecting). Convex faces
// fan-triangulate; non-convex faces (from polygon carve) ear-clip in the face
// plane, so concave loops render correctly.
//=============================================================================

struct BrushTriVertex
{
    Vec3d Position; // world space
    Vec3d Normal;   // world space
    Vec2d Uv;       // from the face's UV projection
};

// Invoked once per face with that face's index, material, and triangle
// vertices (at most 3 * (loop - 2) of them; collinear loop vertices emit no
// triangle). The index is explicit rather than a call-count contract because
// degenerate faces emit nothing.
using BrushFaceEmit = std::function<void(
    std::uint32_t faceIndex, const FaceMaterial&, std::span<const BrushTriVertex>)>;

void BrushTessellate(const BrushMesh& mesh, const Transform3f& transform, const BrushFaceEmit& emit);
void BrushTessellateFace(const BrushMesh& mesh, const Transform3f& transform,
                         std::uint32_t faceIndex, const BrushFaceEmit& emit);
