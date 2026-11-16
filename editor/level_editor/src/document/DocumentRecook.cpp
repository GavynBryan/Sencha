#include "DocumentRecook.h"

#include "DocumentCook.h"

#include <assets/cook/CookedCache.h>
#include <core/assets/AssetPath.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>

namespace
{
    struct DocumentRecookTag {};
}

std::vector<std::string> RecookComposedDependents(std::span<const std::string> savedAssetPaths,
                                                  const std::filesystem::path& assetsRoot,
                                                  double cellSize,
                                                  LoggingProvider& logging,
                                                  RuntimeAssets* assets)
{
    CookedCacheIndex index;
    const std::filesystem::path indexPath = assetsRoot / kCookedCacheDirName / kCookedCacheIndexFileName;
    if (!CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index))
        return {};

    std::vector<std::string> saved;
    for (const std::string& path : savedAssetPaths)
        saved.emplace_back(ContentRelativePath(path));
    std::vector<std::string> dependents;
    for (const auto& [source, entry] : index.Entries())
        if (std::ranges::any_of(entry.AdditionalSources, [&](const CookedAdditionalSource& read) {
                return std::ranges::find(saved, read.RelPath) != saved.end();
            }))
            dependents.push_back(source);
    std::ranges::sort(dependents);

    Logger& log = logging.GetLogger<DocumentRecookTag>();
    for (const std::string& dependent : dependents)
    {
        const DocumentCookResult result =
            CookDocument(assetsRoot / dependent, assetsRoot, cellSize, &logging, assets);
        if (result.Success)
            log.Info("recooked '{}': a scene it composes from was saved", dependent);
        else
            log.Error("recooking '{}' failed: {}", dependent, result.Error);
    }
    return dependents;
}
