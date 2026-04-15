#pragma once

#include <assets/texture/Image.h>
#include <core/assets/AssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <core/handle/LifetimeHandle.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// TextureHandle
//
// Opaque generational handle returned by TextureCache. Encodes a slot index
// and generation counter in a single uint32_t, mirroring ImageHandle.
//=============================================================================
struct TextureHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const TextureHandle&) const = default;
};

//=============================================================================
// TextureEntry
//
// Internal slot type stored by AssetCache. Lives outside TextureCache so it
// can be named in the base class template argument without a forward reference.
//=============================================================================
struct TextureEntry
{
    ImageHandle        GpuImage;
    BindlessImageIndex Bindless;
    VkExtent2D         Extent{};
    uint32_t           Generation = 0;
    uint32_t           RefCount   = 0;
    std::string        PathKey;
};

class TextureCache;
using TextureCacheHandle = LifetimeHandle<TextureCache, TextureHandle>;

//=============================================================================
// TextureCache
//
// Asset-layer service that manages GPU textures. Derives from AssetCache for
// path deduplication, ref-counting, and generational handle management.
//
// TextureCache-specific additions on top of the base:
//   - CreateFromImage(): upload from a caller-supplied CPU Image (no dedup)
//   - GetBindlessIndex(): descriptor index for use in per-instance draw data
//   - GetExtent(): pixel dimensions of the uploaded image
//=============================================================================
class TextureCache : public AssetCache<TextureCache, TextureHandle, TextureEntry>
{
public:
    TextureCache(LoggingProvider& logging,
                 VulkanImageService& images,
                 VulkanDescriptorCache& descriptors,
                 VulkanSamplerCache& samplers);
    ~TextureCache() override;

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&) = delete;
    TextureCache& operator=(TextureCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // -- Load from filesystem -------------------------------------------------
    // Inherited from AssetCache: Acquire(), AcquireOwned(), Release()
    // These call OnLoad() / OnFree() / IsEntryLive() below.
    //
    // `sampler` is consulted only on the first load for a given path.
    [[nodiscard]] TextureHandle     Acquire(std::string_view path,
                                            const SamplerDesc& sampler = {});
    [[nodiscard]] TextureCacheHandle AcquireOwned(std::string_view path,
                                                   const SamplerDesc& sampler = {});

    // -- Upload from CPU-side data --------------------------------------------
    //
    // Uploads `image` directly. No deduplication; refcount starts at 1.
    [[nodiscard]] TextureHandle CreateFromImage(const Image& image,
                                                const SamplerDesc& sampler = {},
                                                const char* debugName = nullptr);

    // -- Accessors ------------------------------------------------------------

    [[nodiscard]] BindlessImageIndex GetBindlessIndex(TextureHandle handle) const;
    [[nodiscard]] VkExtent2D         GetExtent(TextureHandle handle) const;

private:
    friend class AssetCache<TextureCache, TextureHandle, TextureEntry>;

    // AssetCache CRTP hooks.
    // OnLoad is not used directly -- TextureCache::Acquire overloads with SamplerDesc.
    bool OnLoad(std::string_view path, TextureEntry& out);
    void OnFree(TextureEntry& entry);
    bool IsEntryLive(const TextureEntry& entry) const;

    // Uploads `image` to the GPU and populates `out` with the resulting GPU
    // handles. Does not allocate a cache slot. Used by OnLoad and CreateFromImage.
    bool UploadImage(const Image& image, const SamplerDesc& sampler,
                     const char* debugName, TextureEntry& out);

    Logger&                Log;
    VulkanImageService*    Images      = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanSamplerCache*    Samplers    = nullptr;
    bool                   Valid       = false;

    // Pending sampler for use during OnLoad (set by Acquire before base calls OnLoad).
    const SamplerDesc*     PendingSampler = nullptr;
};
