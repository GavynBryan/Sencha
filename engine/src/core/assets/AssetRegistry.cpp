#include <core/assets/AssetRegistry.h>

#include <filesystem>

namespace
{
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
}

bool AssetRegistry::Register(AssetRecord record)
{
    if (record.Path.empty() || record.Type == AssetType::Unknown)
        return false;

    auto [_, inserted] = RecordsByPath.emplace(record.Path, std::move(record));
    return inserted;
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
        return false;

    const std::filesystem::path root{std::string(rootDirectory)};
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return false;

    bool ok = true;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
    {
        if (ec)
        {
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
        record.Path = MakeVirtualAssetPath(root, it->path());
        record.FilePath = it->path().generic_string();
        ok = registry.Register(std::move(record)) && ok;
    }

    return ok;
}
