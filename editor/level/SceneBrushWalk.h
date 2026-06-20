#pragma once

#include "LevelScene.h"

#include <ecs/EntityId.h>

//=============================================================================
// ForEachVisibleBrush — the one place the editor walks the scene's brush
// entities applying the shared visibility (and optionally lock) policy. Visits
// every entity that is visible and has both a brush mesh and a transform; with
// skipLocked, also skips locked entities (the picker policy — renderers pass
// false / hidden-only). Header-only template so the hot render/pick loops keep
// zero call overhead. Pure convenience: callers that need extra per-entity data
// may still iterate GetAllEntities() directly.
//=============================================================================

template <class F>
void ForEachVisibleBrush(const LevelScene& scene, bool skipLocked, F&& fn)
{
    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        if (skipLocked && scene.IsEntityLocked(entity))
            continue;
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        fn(entity, *mesh, *transform);
    }
}
