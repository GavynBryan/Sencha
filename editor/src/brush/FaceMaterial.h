#pragma once

#include <core/assets/AssetRef.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <span>

//=============================================================================
// FaceMaterial / UvProjection — per-face texturing for a brush. (04-§1)
//
// The hard requirement is that UVs survive resize: moving a face, extruding, or
// clipping must NOT stretch the texture. So a face never stores baked (u,v).
// It stores how to PROJECT texture space onto itself; the actual (u,v) of a
// vertex are computed from its position by ProjectUv, at render and cook time.
// Resize moves vertices; the projection is unchanged; the texture stays pinned.
//
// FaceMaterial rides on BrushFace (BrushMesh.h). Because it is a plain member,
// the order-preserving brush ops carry it through edits for free; extrude/clip
// thread it explicitly onto the faces they mint (BrushOps.cpp).
//=============================================================================

struct UvProjection
{
    // Texture axes in brush-local space. For world-aligned textures these come
    // from the face normal's dominant axis (box mapping); for face-aligned
    // they lie in the face plane. Stored explicitly so a designer can rotate and
    // justify freely. Not assumed orthonormal — Scale carries texel density.
    Vec3d AxisU = { 1.0f, 0.0f, 0.0f }; // +U direction in brush-local space
    Vec3d AxisV = { 0.0f, 0.0f, 1.0f }; // +V direction in brush-local space
    Vec2d Scale = { 1.0f, 1.0f };       // world-units per texture tile (texel density)
    Vec2d Offset = { 0.0f, 0.0f };      // texture shift, in UV
    float Rotation = 0.0f;              // degrees, rotates (U,V) in their own plane
    bool  WorldAligned = true;          // true: project from world/box axes (resize lock)
                                        // false: lock to the face as it deforms
};

struct FaceMaterial
{
    AssetRef     Material; // asset://...smat (engine material). Empty = level default.
    UvProjection Uv;
};

// The material a face actually renders/cooks with: its own when set, otherwise the
// level default. The ONE place the "empty face ⇒ level default" rule lives — preview,
// the cook, and PIE resolve through here instead of re-inlining the empty check
// (04-§2). Authored data stays inherit-based; this resolves only at the point of use.
[[nodiscard]] const AssetRef& EffectiveMaterial(const FaceMaterial& face, const AssetRef& levelDefault);

// UV for a vertex of a face, computed from its brush-local position. Never
// stored. Rotation spins the (U,V) basis in its own plane (standard texture
// rotation), then each axis is scaled and offset.
[[nodiscard]] Vec2d ProjectUv(const UvProjection& p, Vec3d localPos);

// Standard projection axes for a face with the given (brush-local) normal.
// World-aligned: U/V are the two world axes orthogonal to the dominant normal
// axis (box mapping: adjacent coplanar brushes tile seamlessly).
// Face-aligned: U/V span the face plane itself, so they follow it as it rotates.
[[nodiscard]] UvProjection UvProjectionForNormal(Vec3d normal, bool worldAligned);

// Justify presets, as pure functions of the projection and the (brush-local)
// points being justified — no mesh/face dependency, so they are unit-testable and
// reusable (editor UV tools, cook-time presets).
// Fit: scale + offset so the points span exactly one tile, [0,1] in both axes.
[[nodiscard]] UvProjection UvProjectionFit(const UvProjection& p, std::span<const Vec3d> localPositions);
// Center: offset so the points' bounds center maps to (0.5, 0.5); scale kept.
[[nodiscard]] UvProjection UvProjectionCenter(const UvProjection& p, std::span<const Vec3d> localPositions);

// A projection expressed in WORLD space: the bridge for cross-brush UV work.
// Brush-local UvProjections convert to and from this, so ONE mapping can span
// faces of entities with different transforms (justify a multi-brush selection
// as a single unit, copy a projection from one brush's face to another's) and
// the texture flows continuously across the seams. Rotation is folded into the
// axes; there is no separate rotation field.
struct WorldUvProjection
{
    Vec3d AxisU = { 1.0f, 0.0f, 0.0f };
    Vec3d AxisV = { 0.0f, 0.0f, 1.0f };
    Vec2d Scale = { 1.0f, 1.0f };
    Vec2d Offset = { 0.0f, 0.0f };
};

// UV of a world-space point under a world projection (the world analog of
// ProjectUv).
[[nodiscard]] Vec2d ProjectWorldUv(const WorldUvProjection& p, Vec3d worldPos);

// Express `local` under localToWorld: for every local point x,
// ProjectWorldUv(result, localToWorld(x)) == ProjectUv(local, x).
// Rotation folds into the returned axes. Zero scale components are treated as 1
// (a degenerate transform cannot be inverted; the result is still total).
[[nodiscard]] WorldUvProjection UvProjectionToWorld(const UvProjection& local,
                                                    const Transform3f& localToWorld);

// Inverse bridge: bake a world projection into a brush's local frame, so the
// face renders the same texture the world projection describes. The result has
// Rotation == 0 (folded into the axes) and is marked WorldAligned (the mapping
// is anchored in world space by construction).
[[nodiscard]] UvProjection UvProjectionToLocal(const WorldUvProjection& world,
                                               const Transform3f& localToWorld);

// Justify presets against WORLD positions: the multi-face, multi-brush fit.
[[nodiscard]] WorldUvProjection WorldUvProjectionFit(const WorldUvProjection& p,
                                                     std::span<const Vec3d> worldPositions);
[[nodiscard]] WorldUvProjection WorldUvProjectionCenter(const WorldUvProjection& p,
                                                        std::span<const Vec3d> worldPositions);
