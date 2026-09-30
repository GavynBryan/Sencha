#pragma once

#include "brush/BrushId.h"

#include <ecs/ComponentAnnotations.h>

// A brush is an editable polygon mesh; the component holds a stable BrushId
// into the EditorScene's BrushMeshStore (heavy mesh data kept out of the
// trivially-copyable component). (03-brush-representation.md §2.2)
//
// The id links the entity to its mesh in the BrushMeshStore sidecar; the mesh
// geometry itself is serialized by EditorDocument (§5). Persisted via
// SceneFieldCodec<BrushId>; the inspector renders it as non-editable.
struct SENCHA_COMPONENT("brush")
       SENCHA_SCHEMA("brush")
       SENCHA_SCENE_CHUNK("BRSH")
BrushComponent
{
    SENCHA_FIELD("id")
    BrushId Id;
};

// The dormant source of a brush baked to a StaticMesh: the entity swapped its
// BrushComponent for a StaticMeshComponent, but its polygon mesh stays in the
// BrushMeshStore under this id so the bake can be reverted (and the entity
// stays pickable through its source shape). Editor-only, like BrushComponent;
// the level cook strips it from the passthrough scene, so it never reaches the
// runtime.
struct SENCHA_COMPONENT("baked_brush")
       SENCHA_SCHEMA("baked_brush")
       SENCHA_SCENE_CHUNK("BKBR")
BakedBrushComponent
{
    SENCHA_FIELD("source")
    BrushId Source;
};

#if !defined(SENCHA_CODEGEN)
#  include "document/BrushComponents.sencha.h"
#endif
