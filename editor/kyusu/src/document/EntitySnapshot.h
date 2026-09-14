#pragma once

#include "brush/BrushId.h"
#include "brush/BrushRecord.h"

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>

#include <optional>

// A type-erased capture of one entity's persistent state, taken through the
// scene serializer registry (so any registered component type is captured with
// no per-type code) plus the editor-only data the registry does not own (the
// brush sidecar record and view flags). Used to make entity deletion undoable.
struct EntitySnapshot
{
    // One object keyed by IComponentSerializer::JsonKey(), matching the
    // per-entity "components" layout SaveSceneJson produces.
    JsonValue Components;
    // The brush sidecar record (mesh and modifier stack, always together) and
    // the id the brush component serialized, present only for a brush entity.
    // The record lives in BrushMeshStore, not the registry; its Revision is
    // meaningless here and reassigned by whichever store restores it.
    std::optional<BrushRecord> Brush;
    BrushId MeshId;
    // The spatial parent, by persistent identity rather than entity handle: the
    // snapshot outlives the handles on both ends. Restore resolves it through
    // the document's PersistentEntityIndex; a parent that no longer resolves
    // restores the entity unparented, matching how destruction orphans a child
    // rather than cascading into it.
    PersistentEntityId ParentId;
    bool Hidden = false;
    bool Locked = false;
};
