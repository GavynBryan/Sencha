#include "data/DataAssetFiles.h"

#include <core/assets/AssetRegistry.h>

std::string NormalizeDataAssetPath(std::string_view relativePath)
{
    std::filesystem::path relative{ std::string(relativePath) };
    if (relative.extension() != ".sdata")
        relative += ".sdata";
    return relative.lexically_normal().generic_string();
}

std::string DataAssetVirtualPath(std::string_view relativePath)
{
    return "asset://" + NormalizeDataAssetPath(relativePath);
}

bool DataAssetPathTaken(const AssetRegistry& registry, const std::filesystem::path& file, std::string_view virtualPath)
{
    return std::filesystem::exists(file) || registry.Contains(virtualPath);
}
