#pragma once

#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageHandle.h>
#include <core/assets/AssetCache.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <string>

struct UiPackageEntry
{
    UiPackage Package;
    std::uint32_t Generation = 0;
    std::uint32_t RefCount = 0;
    std::string PathKey;
};

using UiPackageCacheHandle = Owned<UiPackageHandle>;

//=============================================================================
// UiPackageCache
//
// Path-keyed, ref-counted residency for cooked UI packages. CPU-only.
//
// It owns cooked package data and nothing else. In particular it holds no lease
// on the fonts and textures a package names: those belong to an open screen, so
// that document lifetime and resource lifetime line up. The cache outlives any
// one document built from it, and two screens from one package hold their own
// references to the same resources.
//=============================================================================
class UiPackageCache : public AssetCache<UiPackageCache, UiPackageHandle, UiPackageEntry, AssetType::UiPackage>
{
public:
    explicit UiPackageCache(LoggingProvider& logging);
    ~UiPackageCache() override;

    UiPackageCache(const UiPackageCache&) = delete;
    UiPackageCache& operator=(const UiPackageCache&) = delete;
    UiPackageCache(UiPackageCache&&) = delete;
    UiPackageCache& operator=(UiPackageCache&&) = delete;

    [[nodiscard]] UiPackageHandle Register(std::string_view path, UiPackage package);

    [[nodiscard]] UiPackageHandle Find(std::string_view path) const;
    [[nodiscard]] std::string_view GetName(UiPackageHandle handle) const;

    // Null if the handle is invalid or released.
    [[nodiscard]] const UiPackage* Get(UiPackageHandle handle) const;

private:
    friend class AssetCache<UiPackageCache, UiPackageHandle, UiPackageEntry, AssetType::UiPackage>;

    void OnFree(UiPackageEntry& entry);
    bool IsEntryLive(const UiPackageEntry& entry) const;

    Logger& Log;
};
