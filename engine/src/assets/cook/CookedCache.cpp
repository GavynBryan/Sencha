#include <assets/cook/CookedCache.h>

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

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

    // Freshness-accelerator numbers (sizes, mtimes) serialize as decimal
    // strings: JSON numbers are doubles and cannot carry 64 bits. All are
    // optional in the JSON — absent or malformed reads as 0 (unknown), which
    // just disables the fast path for that entry.
    template <typename T>
    std::string DecimalToString(T value)
    {
        return std::format("{}", value);
    }

    template <typename T>
    T DecimalField(const JsonValue& object, const char* key)
    {
        const JsonValue* field = object.Find(key);
        if (field == nullptr || !field->IsString())
            return 0;
        const std::string& text = field->AsString();
        T out{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return (result.ec == std::errc{} && result.ptr == text.data() + text.size()) ? out : T{};
    }

    bool ParseArtifact(const JsonValue& value, CookedArtifact& out, std::string* error)
    {
        if (!value.IsObject())
            return Fail(error, "artifact entries must be JSON objects");

        const JsonValue* path = value.Find("path");
        if (path == nullptr || !path->IsString() || !IsValidAssetPath(path->AsString()))
            return Fail(error, "artifact 'path' must be an asset path string");

        const JsonValue* file = value.Find("file");
        if (file == nullptr || !file->IsString() || file->AsString().empty())
            return Fail(error, "artifact 'file' must be a non-empty string");

        const JsonValue* type = value.Find("type");
        AssetType assetType = AssetType::Unknown;
        if (type == nullptr || !type->IsString() || !AssetTypeFromString(type->AsString(), assetType))
            return Fail(error, "artifact 'type' must be a known asset type name");

        out.Path = path->AsString();
        out.FileRelPath = file->AsString();
        out.Type = assetType;
        if (const JsonValue* hash = value.Find("hash");
            hash != nullptr && hash->IsString())
            (void)HashFromHex(hash->AsString(), out.ContentHash);
        return true;
    }
} // namespace

const CookedSourceEntry* CookedCacheIndex::Find(std::string_view sourceRelPath) const
{
    auto it = EntriesBySource.find(std::string(sourceRelPath));
    return it == EntriesBySource.end() ? nullptr : &it->second;
}

void CookedCacheIndex::Put(CookedSourceEntry entry)
{
    std::string key = entry.SourceRelPath;
    EntriesBySource.insert_or_assign(std::move(key), std::move(entry));
}

void CookedCacheIndex::Erase(std::string_view sourceRelPath)
{
    EntriesBySource.erase(std::string(sourceRelPath));
}

JsonValue CookedCacheIndex::ToJson() const
{
    std::vector<const CookedSourceEntry*> ordered;
    ordered.reserve(EntriesBySource.size());
    for (const auto& [key, entry] : EntriesBySource)
        ordered.push_back(&entry);
    std::sort(ordered.begin(), ordered.end(),
        [](const CookedSourceEntry* a, const CookedSourceEntry* b)
        { return a->SourceRelPath < b->SourceRelPath; });

    JsonValue::Array sources;
    sources.reserve(ordered.size());
    for (const CookedSourceEntry* entry : ordered)
    {
        JsonValue::Array artifacts;
        artifacts.reserve(entry->Artifacts.size());
        for (const CookedArtifact& artifact : entry->Artifacts)
        {
            JsonValue::Object item;
            item.emplace_back("path", JsonValue(artifact.Path));
            item.emplace_back("file", JsonValue(artifact.FileRelPath));
            item.emplace_back("type", JsonValue(std::string(AssetTypeToString(artifact.Type))));
            if (artifact.ContentHash != 0)
                item.emplace_back("hash", JsonValue(HashToHex(artifact.ContentHash)));
            artifacts.emplace_back(std::move(item));
        }

        JsonValue::Object source;
        source.emplace_back("source", JsonValue(entry->SourceRelPath));
        source.emplace_back("hash", JsonValue(HashToHex(entry->SourceHash)));
        source.emplace_back("size", JsonValue(DecimalToString(entry->SourceSize)));
        source.emplace_back("mtime", JsonValue(DecimalToString(entry->SourceMTime)));
        source.emplace_back("meta_size", JsonValue(DecimalToString(entry->MetaSize)));
        source.emplace_back("meta_mtime", JsonValue(DecimalToString(entry->MetaMTime)));
        source.emplace_back("deps", JsonValue(HashToHex(entry->DependencyHash)));
        source.emplace_back("artifacts", JsonValue(std::move(artifacts)));
        sources.emplace_back(std::move(source));
    }

    JsonValue::Object root;
    root.emplace_back("version", JsonValue(static_cast<double>(kCookedCacheIndexVersion)));
    root.emplace_back("sources", JsonValue(std::move(sources)));
    return JsonValue(std::move(root));
}

bool CookedCacheIndex::FromJson(const JsonValue& root, CookedCacheIndex& out, std::string* error)
{
    if (!root.IsObject())
        return Fail(error, "cooked index root must be a JSON object");

    const JsonValue* version = root.Find("version");
    if (version == nullptr || !version->IsNumber())
        return Fail(error, "missing or non-numeric 'version'");
    if (static_cast<uint32_t>(version->AsNumber()) != kCookedCacheIndexVersion)
        return Fail(error, std::format("unsupported cooked index version {} (expected {})",
                                       version->AsNumber(), kCookedCacheIndexVersion));

    const JsonValue* sources = root.Find("sources");
    if (sources == nullptr || !sources->IsArray())
        return Fail(error, "missing or non-array 'sources'");

    CookedCacheIndex index;
    for (const JsonValue& item : sources->AsArray())
    {
        if (!item.IsObject())
            return Fail(error, "'sources' entries must be JSON objects");

        const JsonValue* source = item.Find("source");
        if (source == nullptr || !source->IsString() || source->AsString().empty())
            return Fail(error, "source 'source' must be a non-empty string");

        const JsonValue* hash = item.Find("hash");
        CookedSourceEntry entry;
        if (hash == nullptr || !hash->IsString() || !HashFromHex(hash->AsString(), entry.SourceHash))
            return Fail(error, "source 'hash' must be a 16-digit hex string");

        // Absent on indices written before version 4; defaulting to zero
        // matches self-contained sources.
        if (const JsonValue* deps = item.Find("deps");
            deps != nullptr && deps->IsString())
        {
            (void)HashFromHex(deps->AsString(), entry.DependencyHash);
        }

        const JsonValue* artifacts = item.Find("artifacts");
        if (artifacts == nullptr || !artifacts->IsArray())
            return Fail(error, "source 'artifacts' must be an array");

        entry.SourceRelPath = source->AsString();
        entry.SourceSize = DecimalField<uint64_t>(item, "size");
        entry.SourceMTime = DecimalField<int64_t>(item, "mtime");
        entry.MetaSize = DecimalField<uint64_t>(item, "meta_size");
        entry.MetaMTime = DecimalField<int64_t>(item, "meta_mtime");
        for (const JsonValue& artifactJson : artifacts->AsArray())
        {
            CookedArtifact artifact;
            if (!ParseArtifact(artifactJson, artifact, error))
                return false;
            entry.Artifacts.push_back(std::move(artifact));
        }

        index.Put(std::move(entry));
    }

    out = std::move(index);
    return true;
}

bool CookedCacheIndex::LoadFromFile(std::string_view path, CookedCacheIndex& out, std::string* error)
{
    std::ifstream file{ std::string(path) };
    if (!file.is_open())
        return Fail(error, std::format("could not open cooked index '{}'", path));

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return Fail(error, std::format("cooked index JSON parse error at {}: {}",
                                       parseError.Position, parseError.Message));

    return FromJson(*root, out, error);
}

bool CookedCacheIndex::SaveToFile(std::string_view path) const
{
    std::ofstream file{ std::string(path), std::ios::trunc };
    if (!file.is_open())
        return false;

    file << JsonStringify(ToJson(), /*pretty*/ true);
    return file.good();
}
