#pragma once

#include <assets/data/DataAssetHandle.h>
#include <core/assets/AssetCache.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

struct DataAssetEntry
{
    std::shared_ptr<const void> Value;
    std::string TypeName;
    uint64_t ReloadVersion = 0;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
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
                                           std::shared_ptr<const void> value);
    [[nodiscard]] bool ReloadInPlace(std::string_view path,
                                     std::string_view typeName,
                                     std::shared_ptr<const void> value);

    [[nodiscard]] DataAssetHandle Acquire(std::string_view path);
    [[nodiscard]] DataAssetCacheHandle AcquireOwned(std::string_view path);
    [[nodiscard]] DataAssetHandle Find(std::string_view path) const;

    [[nodiscard]] const void* GetRaw(DataAssetHandle handle) const;
    [[nodiscard]] std::string_view GetSubtype(DataAssetHandle handle) const;
    [[nodiscard]] uint64_t GetReloadVersion(DataAssetHandle handle) const;
    [[nodiscard]] std::string_view GetName(DataAssetHandle handle) const;
    [[nodiscard]] bool HasResidentSubtype(std::string_view typeName) const;

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

    std::unordered_map<std::string, uint32_t> LiveSubtypeCounts;
};
