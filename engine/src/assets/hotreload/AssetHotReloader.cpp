#include <assets/hotreload/AssetHotReloader.h>

#include <assets/cook/ImportOnDemand.h>
#include <core/assets/AssetLoader.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>

#include <utility>
#include <vector>

namespace
{
    struct AssetHotReloaderTag {};
}

AssetHotReloader::AssetHotReloader(LoggingProvider& logging,
                                   AssetSystem& assets,
                                   AssetRegistry& registry,
                                   const AssetImporterRegistry& importers,
                                   AsyncTaskQueue& tasks,
                                   std::string assetsRoot)
    : Log(logging.GetLogger<AssetHotReloaderTag>())
    , Logging(logging)
    , Assets(assets)
    , Registry(registry)
    , Importers(importers)
    , Tasks(tasks)
    , AssetsRoot(std::move(assetsRoot))
{
}

void AssetHotReloader::ReloadSource(std::string_view sourceRelPath)
{
    std::vector<std::string> artifactPaths;
    if (!ReimportOneSource(AssetsRoot, sourceRelPath, Importers, Logging, artifactPaths))
        return; // re-cook failed; the cook logged it, live assets untouched

    for (const std::string& path : artifactPaths)
    {
        const AssetRecord* record = Registry.FindByPath(path);
        if (record == nullptr)
            continue;
        // Only resident assets need a live swap; for the rest the freshly
        // re-cooked bytes simply apply the next time they're loaded.
        if (!Assets.IsResident(path, record->Type))
            continue;
        StageReload(*record);
    }
}

void AssetHotReloader::StageReload(const AssetRecord& record)
{
    switch (record.Type)
    {
    case AssetType::Texture:
    {
        IAssetSource* source = &Assets.DefaultSource();
        const AssetRecord rec = record; // capture by value for the task thread
        Tasks.Submit<AssetStaging>(
            // Task thread: pure decode of the re-cooked bytes.
            [this, source, rec]() -> AssetStaging {
                return Assets.TextureLoaderRef().LoadStaged(rec, *source);
            },
            // Owner thread, drain point: swap the resident slot in place.
            [this, path = record.Path](AssetStaging staging) {
                if (Assets.TextureLoaderRef().CommitReload(std::move(staging)))
                    Log.Info("AssetHotReloader: reloaded texture '{}'", path);
            });
        break;
    }
    default:
        Log.Debug("AssetHotReloader: '{}' ({}) is not hot-reloadable yet (Stage 6a is textures)",
                  record.Path, AssetTypeToString(record.Type));
        break;
    }
}
