#include <assets/hotreload/AssetHotReloader.h>

#include <assets/cook/AssetImporter.h>
#include <assets/cook/ImportOnDemand.h>
#include <core/assets/AssetStager.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>

#include <filesystem>
#include <string>
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
    // Authored runtime formats (.smat) have no importer: the edited file *is*
    // the asset (Decision B), so there is no cook step — its virtual path is
    // the registry path directly, and only the resident entry is swapped. Any
    // source whose extension no importer claims takes this path.
    const std::filesystem::path relPath{ std::string(sourceRelPath) };
    if (Importers.FindByExtension(relPath.extension().generic_string()) == nullptr)
    {
        const std::string virtualPath = "asset://" + std::string(sourceRelPath);
        const AssetRecord* record = Registry.FindByPath(virtualPath);
        if (record == nullptr)
        {
            Log.Debug("AssetHotReloader: edited source '{}' is not a registered asset",
                      sourceRelPath);
            return;
        }
        if (Assets.IsResident(record->Path, record->Type))
            StageReload(*record);
        return;
    }

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
    const AssetKindRegistration* kind = Assets.Kinds().Find(record.Type);
    if (kind == nullptr || !kind->Reload)
    {
        Log.Debug("AssetHotReloader: '{}' ({}) registers no reload operation",
                  record.Path, AssetTypeToString(record.Type));
        return;
    }

    IAssetStager* stager = kind->Stager;
    IAssetSource* source = &Assets.DefaultSource();
    const AssetRecord rec = record; // capture by value for the task thread

    Tasks.Submit<AssetStaging>(
        // Task thread: pure decode of the re-cooked bytes.
        [stager, source, rec]() -> AssetStaging {
            return stager->LoadStaged(rec, *source);
        },
        // Owner thread, drain point: swap the resident entry in place.
        [this, kindName = kind->Name, reload = kind->Reload, path = record.Path]
        (AssetStaging staging) {
            if (reload(std::move(staging)))
                Log.Info("AssetHotReloader: reloaded {} '{}'", kindName, path);
        });
}
