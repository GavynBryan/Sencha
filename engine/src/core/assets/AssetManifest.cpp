#include <core/assets/AssetManifest.h>

#include <core/assets/AssetRegistry.h>


std::vector<std::string> ResolveManifestPaths(const AssetManifest& manifest,
                                              const AssetRegistry& registry)
{
    std::vector<std::string> paths;
    paths.reserve(manifest.Entries.size());
    for (const AssetManifestEntry& entry : manifest.Entries)
    {
        const AssetRecord* record =
            entry.Id.IsValid() ? registry.FindById(entry.Id) : nullptr;
        paths.push_back(record != nullptr ? record->Path : entry.Path);
    }
    return paths;
}
