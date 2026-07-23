#pragma once

#include "CellArtifactCook.h"
#include "DocumentCook.h"
#include "DocumentCookPaths.h"

#include <core/json/JsonValue.h>

#include <filesystem>
#include <vector>

struct DocumentCookContext;

// Writes the geometry and scene artifacts of one cook into staging: the staged
// cell meshes, the collision sidecar (when collision is emitted), and the
// assembled cooked scene plus its manifest and AssetId map. Appends the cook's
// cell and lightmap entities (`cellEntities`) into the passthrough scene before
// stamping it. Owns the CookedScene progress step. Returns false with the
// context result carrying the error or cancellation; nothing is committed here.
[[nodiscard]] bool WriteCookedSceneArtifacts(
    const DocumentCookContext& ctx,
    JsonValue passthroughScene,
    const std::vector<PendingCellMesh>& meshes,
    JsonValue::Array& cellEntities,
    const std::vector<CellCollisionEntry>& collisionEntries,
    bool emitCollision);
