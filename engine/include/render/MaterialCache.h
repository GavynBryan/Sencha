#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/Owned.h>
#include <render/Material.h>
#include <render/TextureHandle.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct MaterialEntry
{
    Material Value{};
    // RAII references to the textures this material's descriptor indices
    // point at. Releasing the material releases them. Type-erased through
    // ILifetimeOwner, so this header stays backend-free.
    std::vector<TextureCacheHandle> OwnedTextures;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

class MaterialCache;
using MaterialCacheHandle = Owned<MaterialHandle>;

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

    // As Register, additionally transferring ownership of the texture
    // references the material's descriptor indices depend on. They are
    // released when the material entry is freed. If `name` already exists,
    // the existing handle is returned and `ownedTextures` is released
    // immediately (the existing entry already owns its own references).
    [[nodiscard]] MaterialHandle Register(std::string_view name,
                                          const Material& material,
                                          std::vector<TextureCacheHandle> ownedTextures);

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
