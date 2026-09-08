#include <assets/runtime/ContentMount.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>

#include <string>

ContentRootPaths ResolveContentRoot(const std::filesystem::path& root)
{
    return ContentRootPaths{
        .Authored = root,
        .Cooked = root / kCookedCacheDirName,
    };
}

void ScanContentRoot(const ContentRootPaths& root, RuntimeAssets& assets)
{
    ScanAssetsDirectory(
        root.Authored.string(), assets.Registry, assets.Assets.Kinds());
    ScanAssetsDirectory(
        root.Cooked.string(), assets.Registry, assets.Assets.Kinds());
}

void RegisterCookedContent(const ContentRootPaths& root,
                           RuntimeAssets& assets,
                           Logger& log)
{
    RegisterCookedAssets(root.Authored.string(), assets.Registry);

    AssetIdMap idMap;
    std::string idMapError;
    const std::string idMapPath =
        (root.Authored / kAssetIdMapFileName).string();
    if (AssetIdMap::LoadFromFile(idMapPath, idMap, &idMapError))
    {
        ApplyAssetIds(idMap, assets.Registry);
    }
    else
    {
        log.Warn("assets: no asset id map for {} ({}); refs resolve by path only",
                 root.Authored.string(),
                 idMapError);
    }
}

void MountContentRoot(const ContentRootPaths& root,
                      RuntimeAssets& assets,
                      Logger& log)
{
    ScanContentRoot(root, assets);
    RegisterCookedContent(root, assets, log);
}
