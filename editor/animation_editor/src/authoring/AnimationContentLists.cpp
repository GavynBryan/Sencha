#include "authoring/AnimationContentLists.h"

#include <assets/data/DataAssetSubtype.h>
#include <core/assets/AssetRegistry.h>

#include <algorithm>

void AnimationContentLists::Refresh(const AssetRegistry& registry, IAssetSource& source)
{
    ByType.clear();
    BySubtype.clear();
    for (const auto& [path, record] : registry.Records())
    {
        ByType[record.Type].push_back(path);
        if (record.Type == AssetType::Data)
            BySubtype[PeekDataAssetSubtype(source, record)].push_back(path);
    }
    for (auto& [type, paths] : ByType)
        std::ranges::sort(paths);
    for (auto& [subtype, paths] : BySubtype)
        std::ranges::sort(paths);
}

std::span<const std::string> AnimationContentLists::Of(AssetType type) const
{
    const auto found = ByType.find(type);
    return found == ByType.end() ? std::span<const std::string>{} : std::span<const std::string>(found->second);
}

std::span<const std::string> AnimationContentLists::OfSubtype(std::string_view subtype) const
{
    const auto found = BySubtype.find(subtype);
    return found == BySubtype.end() ? std::span<const std::string>{} : std::span<const std::string>(found->second);
}
