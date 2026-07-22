#pragma once

#include "CellArtifactCook.h"
#include "DocumentCook.h"
#include "DocumentCookPaths.h"

#include <core/json/JsonValue.h>

#include <filesystem>
#include <vector>

class CookArtifactTransaction;
class CookStepProgress;
class DocumentArtifactCatalog;
class LoggingProvider;

// Writes the geometry and scene artifacts of one cook into staging: the staged
// cell meshes, the collision sidecar (when collision is emitted), and the
// assembled cooked scene plus its manifest and AssetId map. Appends the cook's
// cell and lightmap entities (`cellEntities`) into the passthrough scene before
// stamping it. Owns the CookedScene progress step. Returns false with `result`
// carrying the error or cancellation; nothing is committed here.
[[nodiscard]] bool WriteCookedSceneArtifacts(
    JsonValue passthroughScene,
    const std::vector<PendingCellMesh>& meshes,
    JsonValue::Array& cellEntities,
    const std::vector<CellCollisionEntry>& collisionEntries,
    bool emitCollision,
    const DocumentArtifactCatalog& catalog,
    const DocumentCookPaths& paths,
    const std::filesystem::path& assetsRoot,
    CookArtifactTransaction& transaction,
    CookStepProgress& progress,
    LoggingProvider& logging,
    DocumentCookResult& result);
