#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/LifetimeHandle.h>
#include <render/Material.h>

#include <cstdint>
#include <string>
#include <string_view>

struct MaterialEntry
{
    Material Value{};
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

class MaterialCache;
using MaterialCacheHandle = LifetimeHandle<MaterialCache, MaterialHandle>;

//=============================================================================
// MaterialCache
//
// CPU-side material cache with stable string keys for scene serialization.
//=============================================================================
class MaterialCache final : public AssetCache<MaterialCache, MaterialHandle, MaterialEntry>
{
public:
    MaterialCache();
    ~MaterialCache() override;

    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;
    MaterialCache(MaterialCache&&) = delete;
    MaterialCache& operator=(MaterialCache&&) = delete;

    [[nodiscard]] MaterialHandle Create(const Material& material);
    [[nodiscard]] MaterialHandle Register(std::string_view name, const Material& material);

    [[nodiscard]] MaterialHandle Acquire(std::string_view name);
    [[nodiscard]] MaterialCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] MaterialHandle Find(std::string_view name) const;
    void Destroy(MaterialHandle handle);

    [[nodiscard]] const Material* Get(MaterialHandle handle) const;
    [[nodiscard]] std::string_view GetName(MaterialHandle handle) const;

private:
    friend class AssetCache<MaterialCache, MaterialHandle, MaterialEntry>;

    bool OnLoad(std::string_view path, MaterialEntry& out);
    void OnFree(MaterialEntry& entry);
    bool IsEntryLive(const MaterialEntry& entry) const;
};
