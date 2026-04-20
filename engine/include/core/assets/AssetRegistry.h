#pragma once

#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

struct AssetRecord
{
    AssetType Type = AssetType::Unknown;
    AssetSourceKind SourceKind = AssetSourceKind::Unknown;

    std::string Path;
    std::string FilePath;

    uint64_t ContentHash = 0;
    uint32_t Version = 1;
};

class AssetRegistry
{
public:
    explicit AssetRegistry(LoggingProvider& logging);

    bool Register(const AssetRecord& record);
    bool RegisterOrVerify(const AssetRecord& record);

    [[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
    [[nodiscard]] bool Contains(std::string_view path) const;

private:
    friend bool ScanAssetsDirectory(std::string_view rootDirectory, AssetRegistry& registry);

    Logger& Log;
    std::unordered_map<std::string, AssetRecord> RecordsByPath;
};

[[nodiscard]] bool IsValidAssetPath(std::string_view path);
bool ScanAssetsDirectory(std::string_view rootDirectory, AssetRegistry& registry);
