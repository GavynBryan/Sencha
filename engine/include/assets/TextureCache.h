#pragma once

#include <assets/Image.h>
#include <core/logging/LoggingProvider.h>
#include <core/raii/ILifetimeOwner.h>
#include <core/raii/LifetimeHandle.h>
#include <core/service/IService.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

// RAII wrapper returned by TextureCache::AcquireOwned(). Calls Release() on
// the cache when it goes out of scope. Move-only; copy is deleted.
// `sizeof(TextureHandle) == 4` fits in the ILifetimeOwner token slot.
class TextureCache;
using TextureCacheHandle = LifetimeHandle<TextureCache, TextureHandle>;

//=============================================================================
// TextureCache
//
// Asset-layer service that sits above VulkanImageService and
// VulkanDescriptorCache. It:
//
//   1. Loads a CPU-side Image (via LoadImageFromFile or caller-supplied data)
//   2. Uploads it to a VkImage through VulkanImageService
//   3. Registers it with VulkanDescriptorCache to get a BindlessImageIndex
//   4. Returns a TextureHandle that game code can hold onto long-term
//
// Handles are stable for the lifetime of the cache. The cache owns every
// GPU image it creates; they are destroyed in ~TextureCache.
//
// Path-based loads are deduplicated: calling Acquire() twice with the same
// path returns the same handle without re-uploading.
//
// Lifetime: path-based loads are refcounted. Each Acquire() / AcquireOwned()
// call for the same path increments the refcount; Release() decrements it and
// frees GPU resources when the count reaches zero. CreateFromImage() starts
// with refcount 1 and has no deduplication. AcquireOwned() wraps the handle in
// a RAII TextureCacheHandle that calls Release() on destruction.
//=============================================================================
class TextureCache : public IService, public ILifetimeOwner
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

    // -- Load from filesystem -----------------------------------------------
    //
    // Loads the image at `path`, uploads it, and returns a stable handle.
    // Identical paths return the same handle without re-uploading; refcount
    // is incremented on each call. Returns an invalid handle on failure.
    // `sampler` is only consulted on the first load for a given path.
    [[nodiscard]] TextureHandle Acquire(std::string_view path,
                                        const SamplerDesc& sampler = {});

    // RAII variant: same as Acquire() but wraps the result in a
    // TextureCacheHandle that calls Release() automatically on destruction.
    [[nodiscard]] TextureCacheHandle AcquireOwned(std::string_view path,
                                                   const SamplerDesc& sampler = {});

    // -- Upload from CPU-side data ------------------------------------------
    //
    // Uploads `image` directly. Useful for procedural textures, tests, and
    // render targets. No deduplication; refcount starts at 1. `debugName` is
    // passed to VulkanImageService for GPU debugging tools.
    [[nodiscard]] TextureHandle CreateFromImage(const Image& image,
                                                const SamplerDesc& sampler = {},
                                                const char* debugName = nullptr);

    // -- Release ------------------------------------------------------------
    //
    // Decrements the refcount for `handle`. When it reaches zero the GPU
    // resources are freed (via VulkanDeletionQueueService), the bindless slot
    // is returned to the pool, and the PathLookup entry is erased so the path
    // can be reloaded fresh. Calling Release() on an invalid or already-released
    // handle is a no-op.
    void Release(TextureHandle handle);

    // -- Accessors ----------------------------------------------------------

    // The bindless descriptor index to write into per-sprite instance data.
    [[nodiscard]] BindlessImageIndex GetBindlessIndex(TextureHandle handle) const;

    // Pixel dimensions of the source image.
    [[nodiscard]] VkExtent2D GetExtent(TextureHandle handle) const;

private:
    struct TextureEntry
    {
        ImageHandle        GpuImage;
        BindlessImageIndex Bindless;
        VkExtent2D         Extent{};
        uint32_t           Generation = 0;
        uint32_t           RefCount   = 0;
        // Non-empty only for path-loaded textures; used to erase PathLookup on Release.
        std::string        PathKey;
    };

    Logger&                Log;
    VulkanImageService*    Images      = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanSamplerCache*    Samplers    = nullptr;
    bool Valid = false;

    std::vector<TextureEntry>                      Entries;
    std::vector<uint32_t>                          FreeSlots;
    std::unordered_map<std::string, TextureHandle> PathLookup;

    // ILifetimeOwner -- called by TextureCacheHandle on construction / destruction.
    void Attach(uint64_t token) override;
    void Detach(uint64_t token) override;

    [[nodiscard]] TextureHandle         UploadAndRegister(const Image& image,
                                                          const SamplerDesc& sampler,
                                                          const char* debugName);
    [[nodiscard]] TextureHandle         AllocHandle(TextureEntry entry);
    [[nodiscard]] TextureEntry*         Resolve(TextureHandle handle);
    [[nodiscard]] const TextureEntry*   Resolve(TextureHandle handle) const;
    [[nodiscard]] static TextureHandle  MakeHandle(uint32_t index, uint32_t generation);
    void                                FreeEntry(uint32_t index, TextureEntry& entry);
};
