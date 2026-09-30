#pragma once

#include "CellArtifactCook.h"
#include "DocumentCook.h"
#include "DocumentCookPaths.h"

#include <core/json/JsonValue.h>

#include <filesystem>
#include <vector>

struct DocumentCookContext;

// Writes the geometry and scene artifacts of one cook into staging: the staged
// cell meshes, the AssetId map, and the assembled .smap, which carries the
// dependency table and the collision cells (produced this cook, or carried
// forward from the active publication when the collision step did not run).
// Appends the cook's cell and lightmap entities (`cellEntities`) into the
// passthrough scene before compiling it. Owns the CookedScene progress step.
// Returns false with the context result carrying the error or cancellation;
// nothing is committed here.
[[nodiscard]] bool WriteCookedSceneArtifacts(
    const DocumentCookContext& ctx,
    JsonValue passthroughScene,
    const std::vector<PendingCellMesh>& meshes,
    JsonValue::Array& cellEntities,
    const std::vector<CellCollisionEntry>& collisionEntries,
    bool emitCollision);
