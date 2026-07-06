#pragma once

#include "brush/BrushId.h"
#include "brush/BrushMesh.h"

#include <core/json/JsonValue.h>

#include <optional>

// A type-erased capture of one entity's persistent state, taken through the
// scene serializer registry (so any registered component type is captured with
// no per-type code) plus the editor-only data the registry does not own (the
// brush sidecar mesh and view flags). Used to make entity deletion undoable.
struct EntitySnapshot
{
    // One object keyed by IComponentSerializer::JsonKey(), matching the
    // per-entity "components" layout SaveSceneJson produces.
    JsonValue Components;
    // The brush sidecar mesh and the id the brush component serialized, present
    // only for a brush entity. The mesh lives in BrushMeshStore, not the registry.
    std::optional<BrushMesh> Mesh;
    BrushId MeshId;
    bool Hidden = false;
    bool Locked = false;
};
