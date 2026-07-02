#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>

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
// Faces are assumed convex + planar, which the brush ops guarantee today; a
// non-convex face (only reachable via future carve) would fan-triangulate
// incorrectly — the cook will gain robust triangulation when that lands.
//=============================================================================

struct BrushTriVertex
{
    Vec3d Position; // world space
    Vec3d Normal;   // world space
    Vec2d Uv;       // from the face's UV projection
};

// Invoked once per face with that face's material and its triangle vertices
// (3 * (loop - 2) of them, fan order).
using BrushFaceEmit = std::function<void(const FaceMaterial&, std::span<const BrushTriVertex>)>;

void BrushTessellate(const BrushMesh& mesh, const Transform3f& transform, const BrushFaceEmit& emit);
