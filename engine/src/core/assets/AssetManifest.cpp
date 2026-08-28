#include <core/assets/AssetManifest.h>

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonValue.h>

#include <unordered_set>

namespace
{
    void CollectInto(const JsonValue& value,
                     std::vector<std::string>& out,
                     std::unordered_set<std::string>& seen)
    {
        if (value.IsString())
        {
            const std::string& text = value.AsString();
            if (IsValidAssetPath(text) && seen.insert(text).second)
                out.push_back(text);
            return;
        }

        if (value.IsArray())
        {
            for (const JsonValue& item : value.AsArray())
                CollectInto(item, out, seen);
            return;
        }

        if (value.IsObject())
        {
            for (const auto& [key, item] : value.AsObject())
                CollectInto(item, out, seen);
        }
    }
} // namespace

std::vector<std::string> CollectAssetPaths(const JsonValue& root)
{
    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    CollectInto(root, paths, seen);
    return paths;
}

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
