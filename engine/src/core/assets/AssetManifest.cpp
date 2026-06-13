#include <core/assets/AssetManifest.h>

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <format>
#include <fstream>
#include <optional>
#include <sstream>
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

    bool Fail(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
        return false;
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

JsonValue AssetManifestToJson(const AssetManifest& manifest)
{
    JsonValue::Array assets;
    assets.reserve(manifest.Entries.size());
    for (const AssetManifestEntry& entry : manifest.Entries)
    {
        JsonValue::Object item;
        if (entry.Id.IsValid())
            item.emplace_back("id", JsonValue(AssetIdToString(entry.Id)));
        item.emplace_back("path", JsonValue(entry.Path));
        assets.emplace_back(std::move(item));
    }

    JsonValue::Object root;
    root.emplace_back("version", JsonValue(static_cast<double>(kAssetManifestVersion)));
    root.emplace_back("assets", JsonValue(std::move(assets)));
    return JsonValue(std::move(root));
}

bool ParseAssetManifestJson(const JsonValue& root, AssetManifest& out, std::string* error)
{
    if (!root.IsObject())
        return Fail(error, "manifest root must be a JSON object");

    const JsonValue* version = root.Find("version");
    if (version == nullptr || !version->IsNumber())
        return Fail(error, "missing or non-numeric 'version'");
    const uint32_t versionNumber = static_cast<uint32_t>(version->AsNumber());
    if (versionNumber != 1 && versionNumber != kAssetManifestVersion)
        return Fail(error, std::format("unsupported manifest version {} (expected 1..{})",
                                       version->AsNumber(), kAssetManifestVersion));

    const JsonValue* assets = root.Find("assets");
    if (assets == nullptr || !assets->IsArray())
        return Fail(error, "missing or non-array 'assets'");

    AssetManifest manifest;
    manifest.Entries.reserve(assets->Size());
    for (const JsonValue& item : assets->AsArray())
    {
        AssetManifestEntry entry;

        // Version 1 entries are bare path strings; version 2 entries are
        // {"id": "<hex>", "path": ...} objects with the id optional.
        if (item.IsString())
        {
            if (versionNumber != 1 || !IsValidAssetPath(item.AsString()))
                return Fail(error, "manifest 'assets' entries must match the manifest version");
            entry.Path = item.AsString();
        }
        else if (item.IsObject() && versionNumber == kAssetManifestVersion)
        {
            const JsonValue* path = item.Find("path");
            if (path == nullptr || !path->IsString() || !IsValidAssetPath(path->AsString()))
                return Fail(error, "manifest entry 'path' must be an asset path string");
            entry.Path = path->AsString();

            if (const JsonValue* id = item.Find("id"); id != nullptr)
            {
                if (!id->IsString())
                    return Fail(error, std::format("manifest entry '{}' has a non-string 'id'",
                                                   entry.Path));
                const std::optional<AssetId> parsed = AssetIdFromString(id->AsString());
                if (!parsed.has_value())
                    return Fail(error, std::format("manifest entry '{}' has a malformed 'id'",
                                                   entry.Path));
                entry.Id = *parsed;
            }
        }
        else
        {
            return Fail(error, "manifest 'assets' entries must match the manifest version");
        }

        manifest.Entries.push_back(std::move(entry));
    }

    out = std::move(manifest);
    return true;
}

bool LoadAssetManifestFile(std::string_view path, AssetManifest& out, std::string* error)
{
    std::ifstream file{ std::string(path) };
    if (!file.is_open())
        return Fail(error, std::format("could not open manifest file '{}'", path));

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return Fail(error, std::format("manifest JSON parse error at {}: {}",
                                       parseError.Position, parseError.Message));

    return ParseAssetManifestJson(*root, out, error);
}

bool WriteAssetManifestFile(std::string_view path, const AssetManifest& manifest)
{
    std::ofstream file{ std::string(path), std::ios::trunc };
    if (!file.is_open())
        return false;

    file << JsonStringify(AssetManifestToJson(manifest), /*pretty*/ true);
    return file.good();
}
