#pragma once

#include <render/Image.h>
#include <render/TextureData.h>
#include <core/assets/AssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <core/handle/Owned.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <render/TextureHandle.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
    // The sampler this entry was registered with — kept so a hot reload
    // (Stage 6) can repoint the same bindless slot at the new image with the
    // original sampler.
    SamplerDesc        Sampler{};
    uint32_t           Generation = 0;
    uint32_t           RefCount   = 0;
    std::string        PathKey;
};

// TextureCacheHandle is declared alongside TextureHandle in
// render/TextureHandle.h so backend-free code can own texture lifetimes.

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

    // Named variant with path deduplication: a second call with the same name
    // returns the existing handle with its refcount incremented (the image is
    // ignored). Used by AssetSystem, which loads bytes itself and registers
    // the result under the asset path.
    [[nodiscard]] TextureHandle CreateFromImage(std::string_view name,
                                                const Image& image,
                                                const SamplerDesc& sampler = {});

    // Cooked-texture upload (docs/assets/pipeline.md, Decisions E/L): takes
    // the format-tagged, mip-tabled TextureData as-is — explicit mip chain,
    // block-compressed formats included; the runtime never generates mips
    // for cooked content. Same name-dedup semantics as CreateFromImage.
    [[nodiscard]] TextureHandle CreateFromTextureData(std::string_view name,
                                                      const TextureData& texture,
                                                      const SamplerDesc& sampler = {});

    // Returns the handle registered under `name` without affecting refcounts,
    // or an invalid handle if none exists.
    [[nodiscard]] TextureHandle Find(std::string_view name) const;

    // -- Hot reload (Stage 6, Decision H) -------------------------------------
    //
    // Swaps a resident texture's GPU image in place: builds the new image,
    // repoints the SAME bindless index at it (so every material referencing
    // the texture renders the new pixels with no further work), and retires
    // the old image through the deletion queue. The handle, slot, generation,
    // and refcount are all preserved — zero handle invalidation. Returns
    // false if `path` has no live entry (nothing to reload).
    [[nodiscard]] bool ReloadInPlace(std::string_view path, const TextureData& texture);
    [[nodiscard]] bool ReloadInPlace(std::string_view path, const Image& image);

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

    // Build-only helpers: create + upload a GPU image, no bindless slot and no
    // cache entry. Invalid handle on failure. Shared by the create and reload
    // paths so the upload logic lives in one place.
    [[nodiscard]] ImageHandle UploadGpuImage(const Image& image, const char* debugName);
    [[nodiscard]] ImageHandle UploadGpuImage(const TextureData& texture, std::string_view name);

    // Reload body: swaps `newImage` into the resident entry for `path`,
    // repointing its bindless slot and deferring the old image. Destroys
    // `newImage` and returns false if `path` is not resident.
    [[nodiscard]] bool ReloadEntryImage(std::string_view path, ImageHandle newImage,
                                        VkExtent2D extent);

    Logger&                Log;
    VulkanImageService*    Images      = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanSamplerCache*    Samplers    = nullptr;
    bool                   Valid       = false;

    // Pending sampler for use during OnLoad (set by Acquire before base calls OnLoad).
    const SamplerDesc*     PendingSampler = nullptr;
};
