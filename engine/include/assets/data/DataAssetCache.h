#pragma once

#include <assets/data/DataAssetHandle.h>
#include <core/assets/AssetCache.h>
#include <core/assets/AssetLease.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct DataAssetEntry
{
    std::shared_ptr<const void> Value;
    std::string TypeName;
    uint64_t ReloadVersion = 0;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    // Leases on what the value's compile declared it depends on, held as long
    // as the value is.
    std::vector<AssetLease> Dependencies;
    // The definitions the value was composed from, transitively.
    std::vector<std::string> Sources;
    bool Alive = false;
};

class DataAssetCache final
    : public AssetCache<DataAssetCache,
                        DataAssetHandle,
                        DataAssetEntry,
                        AssetType::Data>
{
public:
    DataAssetCache();
    ~DataAssetCache() override;

    DataAssetCache(const DataAssetCache&) = delete;
    DataAssetCache& operator=(const DataAssetCache&) = delete;
    DataAssetCache(DataAssetCache&&) = delete;
    DataAssetCache& operator=(DataAssetCache&&) = delete;

    [[nodiscard]] DataAssetHandle Register(std::string_view path,
                                           std::string typeName,
                                           std::shared_ptr<const void> value,
                                           std::vector<AssetLease> dependencies = {},
                                           std::vector<std::string> sources = {});
    // Replaces the value and its dependency set together; the new set is held
    // before the old one is released, so a dependency both share survives.
    [[nodiscard]] bool ReloadInPlace(std::string_view path,
                                     std::string_view typeName,
                                     std::shared_ptr<const void> value,
                                     std::vector<AssetLease> dependencies = {},
                                     std::vector<std::string> sources = {});

    [[nodiscard]] DataAssetHandle Acquire(std::string_view path);
    [[nodiscard]] DataAssetCacheHandle AcquireOwned(std::string_view path);
    [[nodiscard]] DataAssetHandle Find(std::string_view path) const;

    [[nodiscard]] const void* GetRaw(DataAssetHandle handle) const;
    [[nodiscard]] std::string_view GetSubtype(DataAssetHandle handle) const;
    [[nodiscard]] uint64_t GetReloadVersion(DataAssetHandle handle) const;
    [[nodiscard]] std::string_view GetName(DataAssetHandle handle) const;
    [[nodiscard]] bool HasResidentSubtype(std::string_view typeName) const;
    // The definitions an entry was composed from, transitively; empty for one with none.
    [[nodiscard]] std::span<const std::string> GetCompositionSources(DataAssetHandle handle) const;
    // Resident paths composed from `source`, sorted; `source` need not be resident.
    [[nodiscard]] std::vector<std::string> ComposedFrom(std::string_view source) const;

    template<typename T>
    [[nodiscard]] const T* TryGet(DataAssetHandle handle,
                                  std::string_view expectedSubtype) const
    {
        if (GetSubtype(handle) != expectedSubtype)
            return nullptr;
        return static_cast<const T*>(GetRaw(handle));
    }

private:
    friend class AssetCache<DataAssetCache,
                            DataAssetHandle,
                            DataAssetEntry,
                            AssetType::Data>;

    void OnFree(DataAssetEntry& entry);
    bool IsEntryLive(const DataAssetEntry& entry) const;
    void IndexSources(const DataAssetEntry& entry);
    void UnindexSources(const DataAssetEntry& entry);

    std::unordered_map<std::string, uint32_t> LiveSubtypeCounts;
    std::unordered_map<std::string, std::vector<std::string>> DependentsBySource;
};
