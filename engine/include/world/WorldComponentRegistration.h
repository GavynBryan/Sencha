#pragma once

#include <world/ComponentRegistrar.h>
#include <world/held/Held.h>
#include <world/LevelLandmark.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/relocation/RelocationSerial.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/PresentationOffset.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/KeptWithZone.h>
#include <zone/ZoneStateLifetime.h>

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
    // What a drawn pose carries on to the children that inherit it.
    PresentationOffset,
    InheritsPresentationOffset,
    PersistentIdComponent,
    // Which holder a record belongs to: docs/gameplay/held-records.md.
    Held,
    // Which placed scene an entity came from; cooked placements and runtime
    // spawns carry it alike, and SceneInstanceIndex keeps the group live.
    SceneInstance,
    ZoneStateLifetime,
    // Where a body arrives when a level change or a save names it.
    LevelLandmark,
    // A run-time entity its zone keeps: docs/gameplay/saves.md.
    KeptWithZone,
    // How often it was moved discontinuously: docs/gameplay/relocation.md.
    RelocationSerial>;

inline void RegisterWorldComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<WorldComponents>();
}

void RegisterWorldRelocation(World& world);
