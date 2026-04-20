#include <core/assets/AssetRegistry.h>

#include <filesystem>

namespace
{
    constexpr std::string_view AssetPathPrefix = "asset://";

    AssetType AssetTypeFromExtension(std::string_view extension)
    {
        if (extension == ".smesh") return AssetType::Mesh;
        if (extension == ".smat")  return AssetType::Material;
        if (extension == ".stex")  return AssetType::Texture;
        if (extension == ".smap")  return AssetType::Scene;
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

const AssetRecord* AssetRegistry::FindByPath(std::string_view path) const
{
    auto it = RecordsByPath.find(std::string(path));
    return it == RecordsByPath.end() ? nullptr : &it->second;
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
            continue;

        const AssetType type = AssetTypeFromExtension(it->path().extension().generic_string());
        if (type == AssetType::Unknown)
            continue;

        AssetRecord record;
        record.Type = type;
        record.SourceKind = AssetSourceKind::File;
        record.Path = MakeVirtualAssetPath(root, it->path());
        record.FilePath = it->path().generic_string();
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
