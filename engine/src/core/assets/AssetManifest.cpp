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

JsonValue AssetManifestToJson(const AssetManifest& manifest)
{
    JsonValue::Array assets;
    assets.reserve(manifest.Paths.size());
    for (const std::string& path : manifest.Paths)
        assets.emplace_back(path);

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
    if (static_cast<uint32_t>(version->AsNumber()) != kAssetManifestVersion)
        return Fail(error, std::format("unsupported manifest version {} (expected {})",
                                       version->AsNumber(), kAssetManifestVersion));

    const JsonValue* assets = root.Find("assets");
    if (assets == nullptr || !assets->IsArray())
        return Fail(error, "missing or non-array 'assets'");

    AssetManifest manifest;
    manifest.Paths.reserve(assets->Size());
    for (const JsonValue& item : assets->AsArray())
    {
        if (!item.IsString() || !IsValidAssetPath(item.AsString()))
            return Fail(error, "manifest 'assets' entries must be asset path strings");
        manifest.Paths.push_back(item.AsString());
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
