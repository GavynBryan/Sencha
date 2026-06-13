#include <core/assets/AssetIdMap.h>

#include <core/assets/AssetRegistry.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace
{
    bool Fail(std::string* error, std::string message)
    {
        if (error)
            *error = std::move(message);
        return false;
    }

    std::string HashToHex(uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    bool HashFromHex(std::string_view text, uint64_t& out)
    {
        if (text.size() != 16)
            return false;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out, 16);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
} // namespace

AssetId AssetIdMap::FindId(std::string_view path) const
{
    auto it = EntriesByPath.find(std::string(path));
    return it == EntriesByPath.end() ? AssetId{} : it->second.Id;
}

AssetId AssetIdMap::EnsureId(std::string_view path,
                             uint64_t contentHash,
                             const std::function<bool(std::string_view)>& isPathLive)
{
    const std::string key{ path };

    if (auto it = EntriesByPath.find(key); it != EntriesByPath.end())
    {
        if (contentHash != 0 && it->second.ContentHash != contentHash)
        {
            it->second.ContentHash = contentHash;
            Dirty = true;
        }
        return it->second.Id;
    }

    // Rename inheritance: an entry with these exact bytes whose path no
    // longer exists donates its id. Candidates are checked in path order so
    // the choice is deterministic when several dead copies match.
    if (contentHash != 0 && isPathLive)
    {
        std::vector<std::string> candidates;
        for (const auto& [entryPath, entry] : EntriesByPath)
        {
            if (entry.ContentHash == contentHash && !isPathLive(entryPath))
                candidates.push_back(entryPath);
        }
        std::sort(candidates.begin(), candidates.end());

        if (!candidates.empty())
        {
            const AssetId inherited = EntriesByPath.at(candidates.front()).Id;
            EntriesByPath.erase(candidates.front());
            PathsById[inherited] = key;
            EntriesByPath.emplace(key, AssetIdMapEntry{ inherited, contentHash });
            Dirty = true;
            return inherited;
        }
    }

    // First sight: mint from the path so independent checkouts and branches
    // agree, probing past the (astronomically unlikely) collision with an
    // id already bound to a different path.
    AssetId minted{ HashBytes64(path) };
    for (uint64_t salt = 1; !minted.IsValid() || PathsById.contains(minted); ++salt)
        minted.Value = HashBytes64(path, salt);

    PathsById.emplace(minted, key);
    EntriesByPath.emplace(key, AssetIdMapEntry{ minted, contentHash });
    Dirty = true;
    return minted;
}

JsonValue AssetIdMap::ToJson() const
{
    std::vector<const std::pair<const std::string, AssetIdMapEntry>*> ordered;
    ordered.reserve(EntriesByPath.size());
    for (const auto& item : EntriesByPath)
        ordered.push_back(&item);
    std::sort(ordered.begin(), ordered.end(),
        [](const auto* a, const auto* b) { return a->first < b->first; });

    JsonValue::Array assets;
    assets.reserve(ordered.size());
    for (const auto* item : ordered)
    {
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue(AssetIdToString(item->second.Id)));
        entry.emplace_back("path", JsonValue(item->first));
        entry.emplace_back("content_hash", JsonValue(HashToHex(item->second.ContentHash)));
        assets.emplace_back(std::move(entry));
    }

    JsonValue::Object root;
    root.emplace_back("version", JsonValue(static_cast<double>(kAssetIdMapVersion)));
    root.emplace_back("assets", JsonValue(std::move(assets)));
    return JsonValue(std::move(root));
}

bool AssetIdMap::FromJson(const JsonValue& root, AssetIdMap& out, std::string* error)
{
    if (!root.IsObject())
        return Fail(error, "id map root must be a JSON object");

    const JsonValue* version = root.Find("version");
    if (version == nullptr || !version->IsNumber())
        return Fail(error, "missing or non-numeric 'version'");
    if (static_cast<uint32_t>(version->AsNumber()) != kAssetIdMapVersion)
        return Fail(error, std::format("unsupported id map version {} (expected {})",
                                       version->AsNumber(), kAssetIdMapVersion));

    const JsonValue* assets = root.Find("assets");
    if (assets == nullptr || !assets->IsArray())
        return Fail(error, "missing or non-array 'assets'");

    AssetIdMap map;
    for (const JsonValue& item : assets->AsArray())
    {
        if (!item.IsObject())
            return Fail(error, "'assets' entries must be JSON objects");

        const JsonValue* path = item.Find("path");
        if (path == nullptr || !path->IsString() || !IsValidAssetPath(path->AsString()))
            return Fail(error, "entry 'path' must be an asset path string");

        const JsonValue* idText = item.Find("id");
        std::optional<AssetId> id;
        if (idText != nullptr && idText->IsString())
            id = AssetIdFromString(idText->AsString());
        if (!id.has_value())
            return Fail(error, std::format("entry '{}' has a malformed 'id'", path->AsString()));

        AssetIdMapEntry entry{ *id };
        const JsonValue* hash = item.Find("content_hash");
        if (hash == nullptr || !hash->IsString()
            || !HashFromHex(hash->AsString(), entry.ContentHash))
            return Fail(error, std::format("entry '{}' has a malformed 'content_hash'",
                                           path->AsString()));

        // Duplicate paths or ids mean the committed map was hand-mangled;
        // refuse rather than silently keep one side.
        if (map.EntriesByPath.contains(path->AsString()))
            return Fail(error, std::format("duplicate path '{}'", path->AsString()));
        if (map.PathsById.contains(*id))
            return Fail(error, std::format("id '{}' is bound to two paths",
                                           AssetIdToString(*id)));

        map.PathsById.emplace(*id, path->AsString());
        map.EntriesByPath.emplace(path->AsString(), entry);
    }

    out = std::move(map);
    out.Dirty = false;
    return true;
}

bool AssetIdMap::LoadFromFile(std::string_view path, AssetIdMap& out, std::string* error)
{
    std::ifstream file{ std::string(path) };
    if (!file.is_open())
        return Fail(error, std::format("could not open id map '{}'", path));

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return Fail(error, std::format("id map JSON parse error at {}: {}",
                                       parseError.Position, parseError.Message));

    return FromJson(*root, out, error);
}

bool AssetIdMap::SaveToFile(std::string_view path) const
{
    std::ofstream file{ std::string(path), std::ios::trunc };
    if (!file.is_open())
        return false;

    file << JsonStringify(ToJson(), /*pretty*/ true);
    if (!file.good())
        return false;

    Dirty = false;
    return true;
}

std::size_t ApplyAssetIds(const AssetIdMap& map, AssetRegistry& registry)
{
    std::size_t applied = 0;
    for (const auto& [path, entry] : map.Entries())
    {
        if (registry.Contains(path) && registry.AssignId(path, entry.Id))
            ++applied;
    }
    return applied;
}

JsonValue StampAssetRefIds(const JsonValue& root, const AssetIdMap& map)
{
    if (root.IsString())
    {
        const std::string& text = root.AsString();
        if (IsValidAssetPath(text))
        {
            if (const AssetId id = map.FindId(text); id.IsValid())
            {
                JsonValue::Object ref;
                ref.emplace_back("id", JsonValue(AssetIdToString(id)));
                ref.emplace_back("path", JsonValue(text));
                return JsonValue(std::move(ref));
            }
        }
        return root;
    }

    if (root.IsArray())
    {
        JsonValue::Array items;
        items.reserve(root.Size());
        for (const JsonValue& item : root.AsArray())
            items.push_back(StampAssetRefIds(item, map));
        return JsonValue(std::move(items));
    }

    if (root.IsObject())
    {
        JsonValue::Object fields;
        fields.reserve(root.Size());
        for (const auto& [key, item] : root.AsObject())
            fields.emplace_back(key, StampAssetRefIds(item, map));
        return JsonValue(std::move(fields));
    }

    return root;
}
