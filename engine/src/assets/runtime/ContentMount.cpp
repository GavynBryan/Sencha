#include <assets/runtime/ContentMount.h>

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetPath.h>
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

void RegisterCookedContent(const ContentRootPaths& root, RuntimeAssets& assets)
{
    RegisterCookedAssets(root.Authored.string(), assets.Registry);
}

void MountContentRoot(const ContentRootPaths& root, RuntimeAssets& assets)
{
    ScanContentRoot(root, assets);
    RegisterCookedContent(root, assets);
}

void MountCookedFile(const ContentRootPaths& root, RuntimeAssets& assets, const std::filesystem::path& file)
{
    const std::string path = AssetPathOf(std::filesystem::relative(file, root.Cooked).generic_string());
    const AssetRecord* record = assets.Registry.FindByPath(path);
    if (record == nullptr)
    {
        (void)RegisterAssetFile(path, file, assets.Registry, assets.Assets.Kinds());
        return;
    }
    if (!assets.Assets.IsResident(path, record->Type))
        return;
    if (IAssetStager* stager = assets.Assets.LoaderFor(record->Type); stager != nullptr)
        (void)assets.Assets.Reload(
            stager->LoadStaged(*record, assets.Assets.DefaultSource(), *assets.Assets.PathsById()));
}
