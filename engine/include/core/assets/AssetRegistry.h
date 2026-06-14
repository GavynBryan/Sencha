#pragma once

#include <core/assets/AssetId.h>
#include <core/assets/AssetPath.h> // re-exports IsValidAssetPath for registry callers
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

    // Stable identity from the cook's persisted id map (Decision A / Stage
    // 4e). Invalid until ApplyAssetIds runs after import + scan; records
    // register id-less and ids never participate in equivalence checks.
    AssetId Id;

    uint64_t ContentHash = 0;
    uint32_t Version = 1;
};

class AssetRegistry
{
public:
    explicit AssetRegistry(LoggingProvider& logging);

    bool Register(const AssetRecord& record);
    bool RegisterOrVerify(const AssetRecord& record);

    // Binds a stable id to an already-registered path (the ApplyAssetIds
    // pass). Idempotent for the same pair; a record that already carries a
    // different id, or an id already bound to another path, is a conflict
    // and is rejected with a warning.
    bool AssignId(std::string_view path, AssetId id);

    [[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
    [[nodiscard]] const AssetRecord* FindById(AssetId id) const;
    [[nodiscard]] bool Contains(std::string_view path) const;

private:
    friend bool ScanAssetsDirectory(std::string_view rootDirectory, AssetRegistry& registry);

    Logger& Log;
    std::unordered_map<std::string, AssetRecord> RecordsByPath;
    std::unordered_map<AssetId, const AssetRecord*> RecordsById;
};

// IsValidAssetPath lives in <core/assets/AssetPath.h> (included above) so
// low-level data validators can use it without depending on this header.
bool ScanAssetsDirectory(std::string_view rootDirectory, AssetRegistry& registry);

// Name of the cooked-asset cache directory under an assets root
// (docs/assets/pipeline.md, Decision B). The directory scanner skips it —
// cooked artifacts are registered by the import-on-demand driver under
// their intended virtual paths, never discovered by extension.
inline constexpr std::string_view kCookedCacheDirName = ".cooked";
