#include <core/assets/AssetRegistry.h>

#include <core/hash/ContentHash.h>

#include <filesystem>

namespace
{
    constexpr std::string_view AssetPathPrefix = "asset://";

    AssetType AssetTypeFromExtension(std::string_view extension)
    {
        if (extension == ".smesh") return AssetType::StaticMesh;
        if (extension == ".smat")  return AssetType::Material;
        if (extension == ".stex")  return AssetType::Texture;
        if (extension == ".smap")  return AssetType::Scene;
        if (extension == ".sclip") return AssetType::Audio;
        // Source formats (.png, .gltf, ...) are deliberately absent: they
        // reach the registry through import-on-demand, registered under
        // their virtual path against the cooked artifact (Decision B). The
        // Stage 1 loose-PNG mapping retired when the texture cook landed.
        return AssetType::Unknown;
    }

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

bool IsValidAssetPath(std::string_view path)
{
    return path.starts_with(AssetPathPrefix)
        && path.size() > AssetPathPrefix.size()
        && path.find('\\') == std::string_view::npos;
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
        RecordsByPath.emplace(record.Path, record);
        Log.Debug("AssetRegistry: registered {} '{}'",
            std::string(AssetTypeToString(record.Type)), record.Path);
        return true;
    }

    if (!AreEquivalentRecords(it->second, record))
    {
        Log.Warn("AssetRegistry: conflicting asset path '{}' rejected", record.Path);
        return false;
    }

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

    if (it->second.Id.IsValid())
    {
        Log.Warn("AssetRegistry: '{}' already has id {}; rejected reassignment to {}",
            path, AssetIdToString(it->second.Id), AssetIdToString(id));
        return false;
    }

    if (auto bound = RecordsById.find(id); bound != RecordsById.end())
    {
        Log.Warn("AssetRegistry: id {} is already bound to '{}'; rejected for '{}'",
            AssetIdToString(id), bound->second->Path, path);
        return false;
    }

    it->second.Id = id;
    RecordsById.emplace(id, &it->second);
    return true;
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

bool AssetRegistry::Contains(std::string_view path) const
{
    return FindByPath(path) != nullptr;
}

bool ScanAssetsDirectory(std::string_view rootDirectory, AssetRegistry& registry)
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

        const AssetType type = AssetTypeFromExtension(it->path().extension().generic_string());
        if (type == AssetType::Unknown)
            continue;

        AssetRecord record;
        record.Type = type;
        record.SourceKind = AssetSourceKind::File;
        record.Path = MakeVirtualAssetPath(root, it->path());
        record.FilePath = it->path().generic_string();
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
