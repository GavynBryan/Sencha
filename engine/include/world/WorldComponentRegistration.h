#pragma once

#include <world/ComponentRegistrar.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

// Where a thing is, what it hangs off, and what it is called across a save.
//
// The transform trio leads every vocabulary the engine composes. WorldTransform
// and Parent are derived columns rather than authored state, so they are not
// scene-serializable and say so by having no chunk id; they still need storage,
// which is why they are named here beside the one that is.
inline void RegisterWorldComponents(ComponentRegistrar& registrar)
{
    registrar.Add<LocalTransform>();
    registrar.Add<WorldTransform>();
    registrar.Add<Parent>();

    // Per-tick pose history for entities that opt into render interpolation.
    // Derived and runtime-only, like WorldTransform.
    registrar.Add<WorldTransformHistory>();

    registrar.Add<PersistentIdComponent>();
}
