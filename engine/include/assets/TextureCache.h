#pragma once

#include <assets/Image.h>
#include <core/logging/LoggingProvider.h>
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
// Lifetime: v1 is keep-forever (no refcounting). Textures are released only
// when the cache itself is destroyed.
//=============================================================================
class TextureCache : public IService
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
    // Identical paths return the same handle without re-uploading.
    // Returns an invalid handle if the file cannot be read or the upload fails.
    // `sampler` is only used on first load for a given path.
    [[nodiscard]] TextureHandle Acquire(std::string_view path,
                                        const SamplerDesc& sampler = {});

    // -- Upload from CPU-side data ------------------------------------------
    //
    // Uploads `image` directly. Useful for procedural textures, tests, and
    // render targets. No deduplication. `debugName` is passed to
    // VulkanImageService for GPU debugging tools.
    [[nodiscard]] TextureHandle CreateFromImage(const Image& image,
                                                const SamplerDesc& sampler = {},
                                                const char* debugName = nullptr);

    // -- Accessors ----------------------------------------------------------

    // The bindless descriptor index to write into per-sprite instance data.
    [[nodiscard]] BindlessImageIndex GetBindlessIndex(TextureHandle handle) const;

    // Pixel dimensions of the source image.
    [[nodiscard]] VkExtent2D GetExtent(TextureHandle handle) const;

private:
    struct TextureEntry
    {
        ImageHandle       GpuImage;
        BindlessImageIndex Bindless;
        VkExtent2D        Extent{};
        uint32_t          Generation = 0;
    };

    Logger&                Log;
    VulkanImageService*    Images      = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanSamplerCache*    Samplers    = nullptr;
    bool Valid = false;

    std::vector<TextureEntry>                      Entries;
    std::vector<uint32_t>                          FreeSlots;
    std::unordered_map<std::string, TextureHandle> PathLookup;

    [[nodiscard]] TextureHandle         UploadAndRegister(const Image& image,
                                                          const SamplerDesc& sampler,
                                                          const char* debugName);
    [[nodiscard]] TextureHandle         AllocHandle(TextureEntry entry);
    [[nodiscard]] TextureEntry*         Resolve(TextureHandle handle);
    [[nodiscard]] const TextureEntry*   Resolve(TextureHandle handle) const;
    [[nodiscard]] static TextureHandle  MakeHandle(uint32_t index, uint32_t generation);
};
