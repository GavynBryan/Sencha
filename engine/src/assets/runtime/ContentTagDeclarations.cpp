#include <assets/runtime/ContentTagDeclarations.h>

#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <gameplay_tags/GameplayTagDeclarations.h>

#include <algorithm>
#include <format>

void CollectContentTags(RuntimeAssets& assets, std::vector<std::string>& names, std::vector<std::string>& errors)
{
    std::vector<std::string> paths;
    for (const auto& [path, record] : assets.Registry.Records())
        if (record.Type == AssetType::Data
            && PeekDataAssetSubtype(assets.Assets.DefaultSource(), record) == kGameplayTagDeclarationsType)
            paths.push_back(path);
    // Path order, so every machine registers the same names in the same order.
    std::ranges::sort(paths);
    for (const std::string& path : paths)
    {
        // Held only while its names are read: a registered name outlives it.
        AssetLease lease = assets.Assets.LoadLease(path, AssetType::Data);
        const GameplayTagDeclarations* declarations =
            lease ? assets.DataAssets.TryGet<GameplayTagDeclarations>(DataAssetHandle::FromToken(lease.OpaqueToken()),
                                                                       kGameplayTagDeclarationsType)
                  : nullptr;
        if (declarations == nullptr)
        {
            errors.push_back(std::format("{} did not load as {}", path, kGameplayTagDeclarationsType));
            continue;
        }
        names.insert(names.end(), declarations->Tags.begin(), declarations->Tags.end());
    }
}

void DeclareContentTags(RuntimeAssets& assets, GameplayTagRegistry& tags, std::vector<std::string>& errors)
{
    GameplayTagDeclarations all;
    CollectContentTags(assets, all.Tags, errors);
    DeclareGameplayTags(all, tags, errors);
}
