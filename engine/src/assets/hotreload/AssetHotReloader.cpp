#include <assets/hotreload/AssetHotReloader.h>

#include <algorithm>

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

bool AssetHotReloader::ReloadDependents(std::string_view sourceRelPath)
{
    const std::filesystem::path indexPath =
        std::filesystem::path(AssetsRoot) / kCookedCacheDirName / kCookedCacheIndexFileName;
    std::error_code ec;
    if (!std::filesystem::exists(indexPath, ec))
        return false;

    CookedCacheIndex index;
    std::string error;
    if (!CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index, &error))
        return false;

    // Collected before re-cooking any of them: a re-cook rewrites the index, and
    // walking a container while something else replaces it is the kind of bug
    // that only shows up when two documents share a stylesheet.
    std::vector<std::string> dependents;
    for (const auto& [sourcePath, entry] : index.Entries())
    {
        const bool reads = std::any_of(entry.AdditionalSources.begin(),
                                       entry.AdditionalSources.end(),
            [&](const CookedAdditionalSource& extra) { return extra.RelPath == sourceRelPath; });
        if (reads)
            dependents.push_back(sourcePath);
    }

    if (dependents.empty())
        return false;

    // Deterministic, so two documents sharing a stylesheet recook in the same
    // order every time and a diff of what happened is readable.
    std::sort(dependents.begin(), dependents.end());
    for (const std::string& dependent : dependents)
    {
        Log.Info("AssetHotReloader: '{}' changed; recooking '{}' which reads it",
                 sourceRelPath, dependent);
        ReloadSource(dependent);
    }
    return true;
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
            // Not an asset itself, but possibly an input to one: a stylesheet a
            // UI document imports, an include a future shader pulls in. The cook
            // recorded which cooks read it, so this is a lookup rather than a
            // guess -- and without it, editing a shared stylesheet would watch
            // the file, notice the change, and do nothing.
            if (ReloadDependents(sourceRelPath))
                return;

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
