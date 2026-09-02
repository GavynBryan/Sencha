#pragma once

#include <world/ComponentRegistrar.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

// Where a thing is, what it hangs off, and what it is called across a save.
//
// The transform trio leads every vocabulary the engine composes. WorldTransform
// and Parent are derived columns rather than authored state, so they are not
// scene-serializable and say so by having no chunk id; they still need storage,
// which is why they are named here beside the one that is.
using WorldComponents = ComponentSet<
    LocalTransform,
    WorldTransform,
    Parent,
    // Per-tick pose history for entities that opt into render interpolation.
    // Derived and runtime-only, like WorldTransform.
    WorldTransformHistory,
    PersistentIdComponent,
    // Which placed scene an entity came from; cooked placements and runtime
    // spawns carry it alike, and SceneInstanceIndex keeps the group live.
    SceneInstance>;

inline void RegisterWorldComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<WorldComponents>();
}
