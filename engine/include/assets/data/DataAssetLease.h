#pragma once

#include <assets/data/DataAssetCache.h>
#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetLease.h>

#include <string>
#include <string_view>

// One data asset a system reads as its compiled `T`: leased on first use, held for
// as long as this is, and read through the cache, so a hot reload shows on the next
// Get. Docs: docs/assets/architecture.md.
template <typename T>
class DataAssetLease
{
public:
    DataAssetLease() = default;
    DataAssetLease(std::string_view path, std::string_view type)
        : Path(path)
        , Type(type)
    {
    }

    // Null until the asset has loaded and compiled as the named type. A failed load
    // is tried again on the next call.
    [[nodiscard]] const T* Get(AssetSystem& assets, const DataAssetCache& data)
    {
        if (!Lease.IsValid())
            Lease = assets.LoadLease(Path, AssetType::Data);
        return Peek(data);
    }

    // What Get would return, without leasing: for a reader that leased once, when it
    // opened, and reads from const code after. Null until a Get has leased it.
    [[nodiscard]] const T* Peek(const DataAssetCache& data) const
    {
        return Lease.IsValid() ? data.TryGet<T>(Handle(), Type) : nullptr;
    }

    // The cache's handle for the leased asset; invalid until Get has leased it.
    [[nodiscard]] DataAssetHandle Handle() const { return DataAssetHandle::FromToken(Lease.OpaqueToken()); }

    void Reset() { Lease = {}; }

private:
    std::string Path;
    std::string Type;
    AssetLease Lease;
};
