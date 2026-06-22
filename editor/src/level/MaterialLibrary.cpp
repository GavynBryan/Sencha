#include "MaterialLibrary.h"

#include <core/assets/AssetRegistry.h>

#include <algorithm>

namespace
{
    // "asset://materials/dev/gray.smat" -> "materials/dev/gray"
    std::string FriendlyName(std::string_view path)
    {
        constexpr std::string_view scheme = "asset://";
        std::string_view name = path;
        if (name.substr(0, scheme.size()) == scheme)
            name.remove_prefix(scheme.size());
        if (name.size() >= 5 && name.substr(name.size() - 5) == ".smat")
            name.remove_suffix(5);
        return std::string(name);
    }
}

MaterialLibrary::MaterialLibrary(LoggingProvider& logging)
    : Logging(logging)
{
}

void MaterialLibrary::Clear()
{
    Entries.clear();
    ScannedRoot.clear();
}

void MaterialLibrary::Rescan(std::string_view rootDirectory)
{
    Entries.clear();
    ScannedRoot.assign(rootDirectory);
    if (rootDirectory.empty())
        return;

    AssetRegistry registry(Logging);
    ScanAssetsDirectory(rootDirectory, registry);

    for (const auto& [path, record] : registry.Records())
    {
        if (record.Type != AssetType::Material)
            continue;
        Entries.push_back(MaterialAsset{ record.Path, FriendlyName(record.Path) });
    }

    std::sort(Entries.begin(), Entries.end(),
              [](const MaterialAsset& a, const MaterialAsset& b) { return a.Path < b.Path; });
}
