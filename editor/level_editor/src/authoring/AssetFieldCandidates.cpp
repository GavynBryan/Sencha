#include "AssetFieldCandidates.h"

#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetRegistry.h>
#include <core/metadata/RuntimeSchema.h>

#include <algorithm>

std::vector<AssetFieldCandidate> FindAssetFieldCandidates(
    const AssetRegistry& catalog, AssetSystem& assets, const RuntimeField& field)
{
    std::vector<AssetFieldCandidate> entries;
    for (const auto& entry : catalog.Records())
        if (entry.second.Type == field.Asset)
            entries.push_back({ entry.first, entry.second.Id });
    std::sort(entries.begin(), entries.end(),
              [](const AssetFieldCandidate& a, const AssetFieldCandidate& b)
              { return a.Path < b.Path; });

    if (field.Asset != AssetType::Data || field.DataSubtype.empty())
        return entries;

    std::erase_if(entries, [&](const AssetFieldCandidate& entry)
    {
        const AssetRecord* record = catalog.FindByPath(entry.Path);
        return record == nullptr
            || PeekDataAssetSubtype(assets.DefaultSource(), *record) != field.DataSubtype;
    });
    return entries;
}
