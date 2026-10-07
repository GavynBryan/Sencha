#include <core/assets/AssetRegistry.h>

#include <core/assets/AssetKindRegistry.h>

#include <core/assets/AssetIdentity.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace
{
    std::string MakeVirtualAssetPath(const std::filesystem::path& root,
                                     const std::filesystem::path& file)
    {
        std::filesystem::path relative = std::filesystem::relative(file, root);
        return std::string("asset://") + relative.generic_string();
    }

    bool AreEquivalentRecords(const AssetRecord& a, const AssetRecord& b)
    {
        return a.Type == b.Type
            && a.SourceKind == b.SourceKind
            && a.Path == b.Path
            && a.FilePath == b.FilePath
            && a.ContentHash == b.ContentHash
            && a.Version == b.Version;
    }
}

AssetRegistry::AssetRegistry(LoggingProvider& logging)
    : Log(logging.GetLogger<AssetRegistry>())
{
}

bool AssetRegistry::Register(const AssetRecord& record)
{
    if (record.Path.empty())
    {
        Log.Warn("AssetRegistry: rejected asset record with empty path");
        return false;
    }

    if (!IsValidAssetPath(record.Path))
    {
        Log.Warn("AssetRegistry: rejected invalid asset path '{}'", record.Path);
        return false;
    }

    if (record.Type == AssetType::Unknown)
    {
        Log.Warn("AssetRegistry: rejected '{}' with unknown asset type", record.Path);
        return false;
    }

    if (record.SourceKind == AssetSourceKind::Unknown)
    {
        Log.Warn("AssetRegistry: rejected '{}' with unknown asset source kind", record.Path);
        return false;
    }

    const std::string path = record.Path;
    const std::string type = std::string(AssetTypeToString(record.Type));
    auto [it, inserted] = RecordsByPath.emplace(record.Path, record);
    if (!inserted)
    {
        if (AreEquivalentRecords(it->second, record))
            Log.Warn("AssetRegistry: duplicate asset path '{}' rejected", path);
        else
            Log.Warn("AssetRegistry: conflicting asset path '{}' rejected", path);

        return false;
    }

    BindId(it->second);
    Log.Debug("AssetRegistry: registered {} '{}'", type, path);
    return inserted;
}

bool AssetRegistry::RegisterOrVerify(const AssetRecord& record)
{
    if (record.Path.empty())
    {
        Log.Warn("AssetRegistry: rejected asset record with empty path");
        return false;
    }

    if (!IsValidAssetPath(record.Path))
    {
        Log.Warn("AssetRegistry: rejected invalid asset path '{}'", record.Path);
        return false;
    }

    if (record.Type == AssetType::Unknown)
    {
        Log.Warn("AssetRegistry: rejected '{}' with unknown asset type", record.Path);
        return false;
    }

    if (record.SourceKind == AssetSourceKind::Unknown)
    {
        Log.Warn("AssetRegistry: rejected '{}' with unknown asset source kind", record.Path);
        return false;
    }

    auto it = RecordsByPath.find(record.Path);
    if (it == RecordsByPath.end())
    {
        BindId(RecordsByPath.emplace(record.Path, record).first->second);
        Log.Debug("AssetRegistry: registered {} '{}'",
            std::string(AssetTypeToString(record.Type)), record.Path);
        return true;
    }

    if (!AreEquivalentRecords(it->second, record))
    {
        Log.Warn("AssetRegistry: conflicting asset path '{}' rejected", record.Path);
        return false;
    }

    return !record.Id.IsValid() || AssignId(record.Path, record.Id);
}

bool AssetRegistry::Unregister(std::string_view path)
{
    auto it = RecordsByPath.find(std::string(path));
    if (it == RecordsByPath.end())
        return false;

    if (it->second.Id.IsValid())
        RecordsById.erase(it->second.Id);
    RecordsByPath.erase(it);
    return true;
}

bool AssetRegistry::AssignId(std::string_view path, AssetId id)
{
    if (!id.IsValid())
    {
        Log.Warn("AssetRegistry: rejected invalid id for '{}'", path);
        return false;
    }

    auto it = RecordsByPath.find(std::string(path));
    if (it == RecordsByPath.end())
    {
        Log.Warn("AssetRegistry: cannot assign id to unregistered path '{}'", path);
        return false;
    }

    if (it->second.Id == id)
        return true;

    if (auto bound = RecordsById.find(id); bound != RecordsById.end())
    {
        Log.Warn("AssetRegistry: id {} is already bound to '{}'; rejected for '{}'",
            AssetIdToString(id), bound->second->Path, path);
        return false;
    }

    RecordsById.erase(it->second.Id);
    it->second.Id = id;
    RecordsById.emplace(id, &it->second);
    return true;
}

void AssetRegistry::BindId(AssetRecord& record)
{
    const AssetId stated = record.Id;
    if (!record.Id.IsValid())
        record.Id = AssetIdOfPath(record.Path);
    for (uint64_t salt = 1; RecordsById.contains(record.Id); ++salt)
        record.Id = AssetIdOfPath(record.Path, salt);
    if (stated.IsValid() && record.Id != stated)
        Log.Warn("AssetRegistry: '{}' states id {}, which '{}' already holds; it goes by {}", record.Path,
            AssetIdToString(stated), RecordsById.at(stated)->Path, AssetIdToString(record.Id));
    RecordsById.emplace(record.Id, &record);
}

const AssetRecord* AssetRegistry::FindByPath(std::string_view path) const
{
    auto it = RecordsByPath.find(std::string(path));
    return it == RecordsByPath.end() ? nullptr : &it->second;
}

const AssetRecord* AssetRegistry::FindById(AssetId id) const
{
    auto it = RecordsById.find(id);
    return it == RecordsById.end() ? nullptr : it->second;
}

const AssetRecord* AssetRegistry::Find(const AssetRef& ref) const
{
    if (const AssetRecord* byId = ref.Id.IsValid() ? FindById(ref.Id) : nullptr)
        return byId;
    return ref.Path.empty() ? nullptr : FindByPath(ref.Path);
}

AssetRef AssetRegistry::Current(AssetRef ref) const
{
    if (const AssetRecord* byId = ref.Id.IsValid() ? FindById(ref.Id) : nullptr)
        ref.Path = byId->Path;
    else if (const AssetRecord* byPath = ref.Path.empty() ? nullptr : FindByPath(ref.Path))
        ref.Id = byPath->Id;
    return ref;
}

bool AssetRegistry::Contains(std::string_view path) const
{
    return FindByPath(path) != nullptr;
}

bool ScanAssetsDirectory(std::string_view rootDirectory,
                         AssetRegistry& registry,
                         const AssetKindRegistry& kinds)
{
    if (rootDirectory.empty())
    {
        registry.Log.Warn("AssetRegistry: cannot scan empty asset root");
        return false;
    }

    const std::filesystem::path root{std::string(rootDirectory)};
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        registry.Log.Warn("AssetRegistry: asset root '{}' is not a directory", root.generic_string());
        return false;
    }

    bool ok = true;
    std::size_t registered = 0;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
            registry.Log.Warn("AssetRegistry: asset scan skipped entry under '{}': {}",
                root.generic_string(), ec.message());
            ok = false;
            ec.clear();
            continue;
        }

        if (!it->is_regular_file(ec))
        {
            // Never descend into the cooked cache: its artifacts carry
            // virtual paths assigned at cook time, not derived from their
            // physical location (docs/assets/pipeline.md, Decision B).
            if (it->path().filename() == kCookedCacheDirName)
                it.disable_recursion_pending();
            continue;
        }

        // Source formats (.png, .gltf, ...) match no registered kind: they reach
        // the registry through import-on-demand, registered under their virtual
        // path against the cooked artifact (Decision B).
        const AssetType type =
            kinds.FindTypeByExtension(it->path().extension().generic_string());
        if (type == AssetType::Unknown)
            continue;

        AssetRecord record;
        record.Type = type;
        record.SourceKind = AssetSourceKind::File;
        record.Path = MakeVirtualAssetPath(root, it->path());
        record.FilePath = it->path().generic_string();
        record.Id = ReadStatedAssetId(it->path()).value_or(AssetId{});
        if (!HashFileContents(record.FilePath, record.ContentHash))
        {
            registry.Log.Warn("AssetRegistry: could not hash '{}'; registered with no content hash",
                record.FilePath);
            ok = false;
        }
        const bool inserted = registry.Register(record);
        if (inserted)
            ++registered;
        ok = inserted && ok;
    }

    registry.Log.Info("AssetRegistry: scanned '{}' ({} asset records{})",
        root.generic_string(),
        registered,
        ok ? "" : ", with errors");
    return ok;
}

int RegisterCookedAssets(std::string_view assetsRoot, AssetRegistry& registry)
{
    const std::filesystem::path indexPath =
        std::filesystem::path(std::string(assetsRoot)) / kCookedCacheDirName / "index.json";

    std::ifstream file(indexPath);
    if (!file.is_open())
        return 0; // no cooked index: physical scan still covers loose runtime files

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::optional<JsonValue> json = JsonParse(buffer.str());
    if (!json || !json->IsObject())
        return 0;

    const JsonValue* sources = json->Find("sources");
    if (sources == nullptr || !sources->IsArray())
        return 0;

    int registered = 0;
    for (const JsonValue& source : sources->AsArray())
    {
        const JsonValue* artifacts = source.IsObject() ? source.Find("artifacts") : nullptr;
        if (artifacts == nullptr || !artifacts->IsArray())
            continue;

        for (const JsonValue& artifact : artifacts->AsArray())
        {
            if (!artifact.IsObject())
                continue;
            const JsonValue* path = artifact.Find("path");
            const JsonValue* fileRel = artifact.Find("file");
            const JsonValue* typeName = artifact.Find("type");
            if (path == nullptr || !path->IsString() || fileRel == nullptr || !fileRel->IsString()
                || typeName == nullptr || !typeName->IsString())
                continue;

            // The physical scan already keyed cooked runtime files (.smesh, ...)
            // by location; this only needs to add the ones it cannot key, and
            // give every one the id its source states.
            const AssetId id = StatedAssetId(artifact).value_or(AssetId{});
            if (registry.Contains(path->AsString()))
            {
                if (id.IsValid())
                    (void)registry.AssignId(path->AsString(), id);
                continue;
            }

            AssetType type = AssetType::Unknown;
            if (!AssetTypeFromString(typeName->AsString(), type))
                continue;

            AssetRecord record;
            record.Type = type;
            record.SourceKind = AssetSourceKind::File;
            record.Path = path->AsString();
            record.Id = id;
            record.FilePath =
                (std::filesystem::path(std::string(assetsRoot)) / fileRel->AsString()).generic_string();
            if (registry.Register(record))
                ++registered;
        }
    }

    return registered;
}
