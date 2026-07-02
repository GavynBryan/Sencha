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
    ScannedRoots.clear();
}

void MaterialLibrary::Rescan(std::span<const std::string> roots)
{
    // Copy first: callers may pass the previously scanned roots (Roots()),
    // which alias the vector this replaces.
    std::vector<std::string> scanned(roots.begin(), roots.end());
    Entries.clear();
    ScannedRoots = std::move(scanned);

    for (const std::string& root : ScannedRoots)
    {
        if (root.empty())
            continue;

        AssetRegistry registry(Logging);
        ScanAssetsDirectory(root, registry);

        for (const auto& [path, record] : registry.Records())
        {
            if (record.Type != AssetType::Material)
                continue;
            Entries.push_back(MaterialAsset{ record.Path, FriendlyName(record.Path) });
        }
    }

    std::sort(Entries.begin(), Entries.end(),
              [](const MaterialAsset& a, const MaterialAsset& b) { return a.Path < b.Path; });
    // Two roots carrying the same root-relative path resolve to one asset://
    // ref at load time; list it once.
    Entries.erase(std::unique(Entries.begin(), Entries.end(),
                              [](const MaterialAsset& a, const MaterialAsset& b) {
                                  return a.Path == b.Path;
                              }),
                  Entries.end());
}
