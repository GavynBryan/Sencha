#pragma once

#include <filesystem>
#include <string>
#include <string_view>

class AssetRegistry;

// A path under a content root, with the data extension and forward slashes.
[[nodiscard]] std::string NormalizeDataAssetPath(std::string_view relativePath);
[[nodiscard]] std::string DataAssetVirtualPath(std::string_view relativePath);
[[nodiscard]] bool DataAssetPathTaken(const AssetRegistry& registry, const std::filesystem::path& file,
                                      std::string_view virtualPath);
void RegisterDataAssetFile(AssetRegistry& registry, std::string_view virtualPath, const std::filesystem::path& file);
