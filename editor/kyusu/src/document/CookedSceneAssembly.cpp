#include "CookedSceneAssembly.h"

#include "CookArtifactTransaction.h"
#include "CookGraph.h"
#include "CookStepCache.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookContext.h"
#include "DocumentSerialization.h"

#include <assets/cook/SceneCookOutput.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <world/scene/SmapFormat.h>

#include <string>
#include <string_view>
#include <system_error>
#include <utility>

bool WriteCookedSceneArtifacts(const DocumentCookContext& ctx,
                               JsonValue passthroughScene,
                               const std::vector<PendingCellMesh>& meshes,
                               JsonValue::Array& cellEntities,
                               const std::vector<CellCollisionEntry>& collisionEntries,
                               bool emitCollision)
{
    const DocumentArtifactCatalog& catalog = ctx.Catalog;
    const DocumentCookPaths& paths = ctx.Paths;
    const std::filesystem::path& assetsRoot = ctx.AssetsRoot;
    CookArtifactTransaction& transaction = ctx.Transaction;
    CookStepProgress& progress = ctx.Progress;
    LoggingProvider& logging = ctx.Logging;
    DocumentCookResult& result = ctx.Result;

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

    // Collision cells fold into the .smap. A cook that skipped the collision
    // step carries the active publication's cells forward, the same Preserve
    // semantics the blob artifacts themselves get from the publication plan.
    std::vector<SmapCollisionCell> collisionCells;
    if (emitCollision)
    {
        collisionCells.reserve(collisionEntries.size());
        for (const CellCollisionEntry& entry : collisionEntries)
            collisionCells.push_back(
                SmapCollisionCell{ entry.BlobRelPath, entry.Origin });
    }
    else
    {
        SmapContents prior;
        SmapError priorError;
        if (!ReadSmapMetadataFile(paths.Scene, prior, &priorError))
        {
            result.Error = "CookDocument: cannot carry collision forward: "
                + priorError.Message;
            return false;
        }
        collisionCells = std::move(prior.Collision);
    }

    // asset:// resolution: Generated cell meshes live under .cooked/; every other
    // ref (materials, their textures) is an authored asset under the root.
    const auto physicalPathFor =
        [&assetsRoot, &catalog, &transaction](
            std::string_view assetPath) -> std::filesystem::path {
            constexpr std::string_view prefix = "asset://";
            const std::string rel(assetPath.substr(prefix.size()));
            if (catalog.IsGenerated(assetPath))
            {
                // A preserved generated asset already exists in the active tree;
                // staging it would register an unwritten file the commit rejects.
                if (catalog.IsPreserved(".cooked/" + rel))
                    return assetsRoot / ".cooked" / rel;
                return transaction.Stage(assetsRoot / ".cooked" / rel);
            }
            return assetsRoot / rel;
        };

    const std::filesystem::path idMapPath = assetsRoot / kAssetIdMapFileName;
    std::string cookError;
    if (!transaction.Seed(idMapPath, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return false;
    }
    if (!WriteCookedScene(cooked, catalog.SceneRefs(), collisionCells,
            EditorSceneSerializers(), physicalPathFor,
            transaction.Stage(idMapPath), transaction.Stage(paths.Scene),
            &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return false;
    }
    return true;
}
