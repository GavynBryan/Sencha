#pragma once

#include "EditorScene.h"

#include "brush/BrushEvaluation.h"
#include "brush/BrushWorkCounters.h"

#include <ecs/EntityId.h>

//=============================================================================
// ForEachVisibleBrushPiece — the one place the editor walks the scene's brush
// geometry applying the shared visibility (and optionally lock) policy. Visits
// every PIECE the brush evaluates to (its modifier stack applied): for a brush
// without modifiers that is its mesh once, at the entity transform. With
// skipLocked, also skips locked entities (the picker policy — renderers pass
// false / hidden-only). Header-only template so the hot render/pick loops keep
// zero call overhead. Callers that need source-only geometry (element handles,
// mesh edits) read TryGetBrushMesh directly rather than walking pieces.
//=============================================================================

// Which brushes a walk visits. Geometry consumers (the cook, solid drawing, ray
// casts against surfaces) take Solids: a volume is air and stands in for nothing.
enum class BrushRoleFilter : std::uint8_t
{
    Solids,
    Volumes,
    All,
};

[[nodiscard]] inline bool Admits(BrushRoleFilter filter, BrushRole role)
{
    return filter == BrushRoleFilter::All || (role == BrushRole::Volume) == (filter == BrushRoleFilter::Volumes);
}

template <class F>
void ForEachVisibleBrushPiece(const EditorScene& scene, bool skipLocked, BrushRoleFilter roles, F&& fn)
{
    ++BrushWorkCounters::Frame().PieceWalks;
    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        if (skipLocked && scene.IsEntityEffectivelyLocked(entity))
            continue;
        if (scene.TryGetBrush(entity) == nullptr)
            continue; // a baked brush draws as its placed mesh, not its dormant source
        if (!Admits(roles, scene.RoleOf(entity)))
            continue;
        const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (evaluated == nullptr || transform == nullptr)
            continue;
        for (const BrushPiece& piece : evaluated->Pieces)
            fn(entity, piece, PieceWorldTransform(*transform, piece));
    }
}

// The authored mesh alone, at the entity transform, for consumers that address
// mesh elements (edge and vertex handles, material scans): generated pieces
// are never element targets, so they are not visited here.
template <class F>
void ForEachVisibleBrushSource(const EditorScene& scene, bool skipLocked, BrushRoleFilter roles, F&& fn)
{
    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        if (skipLocked && scene.IsEntityEffectivelyLocked(entity))
            continue;
        if (!Admits(roles, scene.RoleOf(entity)))
            continue;
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        fn(entity, *mesh, *transform);
    }
}

// The mesh-and-transform form most consumers want: each piece's mesh at its
// world placement. The piece identity is dropped; use the piece form when the
// ordinal matters.
template <class F>
void ForEachVisibleBrush(const EditorScene& scene, bool skipLocked, BrushRoleFilter roles, F&& fn)
{
    ForEachVisibleBrushPiece(scene, skipLocked, roles,
        [&](EntityId entity, const BrushPiece& piece, const Transform3f& world)
        { fn(entity, *piece.Mesh, world); });
}
