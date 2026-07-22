#include "CookedSceneAssembly.h"

#include "CookArtifactTransaction.h"
#include "CookGraph.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"

#include <assets/cook/SceneCookOutput.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/json/JsonStringify.h>

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

bool WriteCookedSceneArtifacts(JsonValue passthroughScene,
                               const std::vector<PendingCellMesh>& meshes,
                               JsonValue::Array& cellEntities,
                               const std::vector<CellCollisionEntry>& collisionEntries,
                               bool emitCollision,
                               const DocumentArtifactCatalog& catalog,
                               const DocumentCookPaths& paths,
                               const std::filesystem::path& assetsRoot,
                               CookArtifactTransaction& transaction,
                               CookStepProgress& progress, LoggingProvider& logging,
                               DocumentCookResult& result)
{
    MeshSerializer serializer(logging);
    for (const PendingCellMesh& pending : meshes)
        if (!serializer.WriteToFile(pending.Physical.generic_string(), pending.Geometry))
        {
            result.Error = "CookDocument: could not write mesh '"
                + pending.Physical.generic_string() + "'";
            return false;
        }

    JsonValue cooked = std::move(passthroughScene);
    progress.Begin(DocumentCookStepIds::CookedScene);
    bool appended = false;
    if (cooked.IsObject())
        for (auto& [key, value] : cooked.AsObject())
            if (key == "entities" && value.IsArray())
            {
                for (JsonValue& cellEntity : cellEntities)
                    value.AsArray().push_back(std::move(cellEntity));
                appended = true;
                break;
            }
    if (!appended)
    {
        result.Error = "CookDocument: assembled scene has no entities array";
        return false;
    }
    progress.Complete();
    if (progress.Cancelled(result))
        return false;

    std::error_code ec;
    std::filesystem::create_directories(paths.CookedDir, ec);

    // Collision sidecar: the runtime loads this at map load (LoadZoneCollision) to
    // spawn the level's static brush colliders. Empty array if no brushes.
    if (emitCollision)
    {
        JsonValue::Array sidecar;
        sidecar.reserve(collisionEntries.size());
        for (const CellCollisionEntry& entry : collisionEntries)
            sidecar.push_back(JsonValue(JsonValue::Object{
                { "blob", JsonValue(entry.BlobRelPath) },
                { "origin", JsonValue(JsonValue::Array{
                    JsonValue(static_cast<double>(entry.Origin.X)),
                    JsonValue(static_cast<double>(entry.Origin.Y)),
                    JsonValue(static_cast<double>(entry.Origin.Z)) }) },
            }));
        std::ofstream sidecarFile(transaction.Stage(paths.Collision));
        sidecarFile << JsonStringify(JsonValue(std::move(sidecar)), /*pretty*/ true);
        if (!sidecarFile.good())
        {
            result.Error = "CookDocument: could not write collision sidecar";
            return false;
        }
    }

    // asset:// resolution: Generated cell meshes live under .cooked/; every other
    // ref (materials, their textures) is an authored asset under the root.
    const auto physicalPathFor =
        [&assetsRoot, &catalog, &transaction](
            std::string_view assetPath) -> std::filesystem::path {
            constexpr std::string_view prefix = "asset://";
            const std::string rel(assetPath.substr(prefix.size()));
            if (catalog.IsGenerated(assetPath))
                return transaction.Stage(assetsRoot / ".cooked" / rel);
            return assetsRoot / rel;
        };

    const std::filesystem::path idMapPath = assetsRoot / kAssetIdMapFileName;
    std::string cookError;
    if (!transaction.Seed(idMapPath, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return false;
    }
    const std::filesystem::path stagedManifest = transaction.Stage(paths.Manifest);
    const std::filesystem::path stagedScene = transaction.Stage(paths.Scene);
    if (!WriteCookedScene(cooked, catalog.SceneRefs(), physicalPathFor,
            transaction.Stage(idMapPath), stagedManifest, stagedScene, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return false;
    }
    return true;
}
