#include "DataEditorWorkspace.h"

#include "data/DataAssetFiles.h"
#include "project/Project.h"

#include <algorithm>

namespace
{
    std::filesystem::path FirstContentRoot(const ProjectDescriptor& project)
    {
        return project.ContentRoots.empty() ? std::filesystem::path{}
                                            : std::filesystem::path(project.ContentRoots.front());
    }
}

DataEditorWorkspace::DataEditorWorkspace(RuntimeAssets& assets, const ProjectDescriptor& project)
    : Assets(assets)
    , ContentRoot(FirstContentRoot(project))
    , Documents(assets, Sources, { .ContentRoot = ContentRoot, .Subtypes = {} })
{
}

bool DataEditorWorkspace::Duplicate(std::string_view virtualPath, std::string_view relativePath, std::string& error)
{
    const AssetRecord* source = Assets.Registry.FindByPath(virtualPath);
    if (source == nullptr || source->Type != AssetType::Data || ContentRoot.empty())
    {
        error = "source data asset is not registered";
        return false;
    }
    const std::string relative = NormalizeDataAssetPath(relativePath);
    const std::filesystem::path destination = ContentRoot / relative;
    const std::string destinationVirtual = DataAssetVirtualPath(relative);
    if (DataAssetPathTaken(Assets.Registry, destination, destinationVirtual))
    {
        error = "destination already exists";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (!ec)
        std::filesystem::copy_file(source->FilePath, destination, std::filesystem::copy_options::none, ec);
    if (ec)
    {
        error = ec.message();
        return false;
    }
    RegisterDataAssetFile(Assets.Registry, destinationVirtual, destination);
    return Documents.OpenOrFocus(destinationVirtual, error) != nullptr;
}

bool DataEditorWorkspace::Rename(std::string_view virtualPath, std::string_view relativePath,
                                 DirtyDisposition disposition, std::string& error)
{
    const AssetRecord* source = Assets.Registry.FindByPath(virtualPath);
    if (source == nullptr || source->Type != AssetType::Data || ContentRoot.empty())
    {
        error = "source data asset is not registered";
        return false;
    }
    const std::string relative = NormalizeDataAssetPath(relativePath);
    const std::filesystem::path destination = ContentRoot / relative;
    const std::string destinationVirtual = DataAssetVirtualPath(relative);
    if (DataAssetPathTaken(Assets.Registry, destination, destinationVirtual))
    {
        error = "destination already exists";
        return false;
    }
    const std::filesystem::path file = source->FilePath;
    if (!CloseIfOpen(virtualPath, disposition, error))
        return false;

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (!ec)
        std::filesystem::rename(file, destination, ec);
    if (ec)
    {
        error = ec.message();
        return false;
    }
    (void)Assets.Registry.Unregister(virtualPath);
    RegisterDataAssetFile(Assets.Registry, destinationVirtual, destination);
    return Documents.OpenOrFocus(destinationVirtual, error) != nullptr;
}

bool DataEditorWorkspace::Delete(std::string_view virtualPath, DirtyDisposition disposition, std::string& error)
{
    const AssetRecord* record = Assets.Registry.FindByPath(virtualPath);
    if (record == nullptr || record->Type != AssetType::Data)
    {
        error = "data asset is not registered";
        return false;
    }
    const std::filesystem::path file = record->FilePath;
    if (!CloseIfOpen(virtualPath, disposition, error))
        return false;

    std::error_code ec;
    std::filesystem::remove(file, ec);
    if (ec)
    {
        error = ec.message();
        return false;
    }
    (void)Assets.Registry.Unregister(virtualPath);
    return true;
}

std::vector<const AssetRecord*> DataEditorWorkspace::DataAssets() const
{
    std::vector<const AssetRecord*> records;
    for (const auto& [path, record] : Assets.Registry.Records())
        if (record.Type == AssetType::Data)
            records.push_back(&record);
    std::ranges::sort(records, {}, &AssetRecord::Path);
    return records;
}

bool DataEditorWorkspace::CloseIfOpen(std::string_view virtualPath, DirtyDisposition disposition, std::string& error)
{
    const std::optional<std::size_t> index = Documents.IndexOf(virtualPath);
    return !index || Documents.Close(*index, disposition, error);
}
