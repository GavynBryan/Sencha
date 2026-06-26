#pragma once

#include <assets/cook/BrushGeometryCook.h> // CookFace
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>

#include <span>
#include <vector>

//=============================================================================
// Spatial clustering for the level cook (docs/plans/sencha-level-editor/
// 05-level-cook.md, Revision step 3). Dev-only (SENCHA_ENABLE_COOK), pure.
//
// Buckets whole brushes into a world-space grid so the cook emits one mesh +
// one StaticMeshComponent per cell. A single zone monolith has one bounding
// volume, so frustum/occlusion culling would be all-or-nothing across a whole
// zone (50-1000 brushes); per-cell meshes keep culling granular.
//
// Pipeline position (the rest are named seams that slot in without reshaping
// this one): collect -> [modifier-expand / classify: future] -> CLUSTER ->
// [instanced emission: future] -> bake per cell. A brush is the atomic unit
// here and is never split, which is exactly what modifier expansion and
// instancing operate on.
//=============================================================================

// One brush's cookable geometry, kept whole. Faces are world-space (the editor
// fills them straight from BrushTessellate); WorldBounds is the brush's world
// AABB, used only to choose its cell.
struct CookBrushGeometry
{
    Aabb3d                WorldBounds;
    std::vector<CookFace> Faces;
};

// One spatial cell's merged geometry. Coord is the integer grid cell. Origin is
// its lattice corner (Coord * cellSize) in world space: the cell's frame is
// defined by the grid, not its contents, so editing one brush only moves that
// brush's vertices, not the whole cell. Faces are CELL-LOCAL (Origin subtracted
// from every vertex position), so a runtime StaticMeshComponent placed at
// LocalTransform = translate(Origin) reproduces the original world position.
struct BrushCell
{
    Vec3i                 Coord;
    Vec3d                 Origin;
    std::vector<CookFace> Faces;
};

// Assigns each non-empty brush wholly to the cell its bounds-center falls in
// (floor(center / cellSize) per axis); a brush is never split across cells.
// Cells are returned in ascending (x, y, z) coord order and brushes keep input
// order within a cell, so the cook output is deterministic regardless of the
// caller's container iteration order. cellSize must be > 0 (the feeding cvar is
// clamped); a non-positive size degenerates to a single cell at the world origin.
[[nodiscard]] std::vector<BrushCell>
ClusterBrushesIntoCells(std::span<const CookBrushGeometry> brushes, double cellSize);
