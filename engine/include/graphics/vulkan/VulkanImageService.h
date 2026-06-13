#pragma once

#include <core/handle/Handle.h>
#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

class VulkanAllocatorService;
class VulkanDeletionQueueService;
class VulkanDeviceService;
class VulkanUploadContextService;

//=============================================================================
// VulkanImageService
//
// Owns every VkImage + default VkImageView the engine allocates. Images are
// addressed by opaque ImageHandle values with generational validation,
// mirroring VulkanBufferService.
//
// The service deliberately handles only the 90% path: 2D color images,
// single array layer, optional mip chain, default whole-image view.
// Cubemap, volumetric, and non-default-view images are out of scope until
// a feature actually needs them.
//
// This service deals in Vulkan images, not "textures". A higher layer
// (resource cache / asset system) composes ImageHandles with sampler
// state into whatever Sencha-level Texture type the engine exposes to
// gameplay code.
//
// Upload() is synchronous, blocking on a fence, and transitions the image
// to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when it returns. The
// bindless sampled-image array in VulkanDescriptorCache expects every
// bound image to sit in that layout.
//
// Upload runs on the graphics queue to avoid queue-family ownership
// transfers. Same rationale as VulkanBufferService.
//=============================================================================
// Generational handle to a GPU image owned by VulkanImageService. One of the
// engine's unified Handle<Tag> types (handle convergence).
using ImageHandle = Handle<struct ImageHandleTag>;

struct ImageCreateInfo
{
    VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;
    VkExtent2D Extent{};
    VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t MipLevels = 1;          // 0 or 1 => single mip. >1 => explicit chain length.
    bool GenerateMips = false;       // auto-blit mip chain from base after upload
    const char* DebugName = nullptr;
};

class VulkanImageService : public IService
{
public:
    VulkanImageService(LoggingProvider& logging,
                       VulkanDeviceService& device,
                       VulkanAllocatorService& allocator,
                       VulkanUploadContextService& upload,
                       VulkanDeletionQueueService& deletionQueue);
    ~VulkanImageService() override;

    VulkanImageService(const VulkanImageService&) = delete;
    VulkanImageService& operator=(const VulkanImageService&) = delete;
    VulkanImageService(VulkanImageService&&) = delete;
    VulkanImageService& operator=(VulkanImageService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // -- Resource lifetime --------------------------------------------------

    [[nodiscard]] ImageHandle Create(const ImageCreateInfo& info);
    void Destroy(ImageHandle handle);

    // -- Data movement ------------------------------------------------------
    //
    // Upload tightly-packed pixel data for the base mip level. The service
    // handles the layout transitions and optional mip generation. On
    // success the image is left in SHADER_READ_ONLY_OPTIMAL.
    bool Upload(ImageHandle handle, const void* data, VkDeviceSize size);

    // One mip's extent and byte offset within the blob handed to UploadMips.
    struct MipUploadRegion
    {
        uint32_t MipLevel = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        VkDeviceSize Offset = 0;
    };

    // Upload a pre-generated mip chain in one staging pass: `data` is the
    // packed blob, `regions` describe each level. For cooked textures — the
    // runtime never generates mips for them (docs/assets/pipeline.md,
    // Decision L) — so the image must have been created with an explicit
    // MipLevels count and GenerateMips off. On success the image is left in
    // SHADER_READ_ONLY_OPTIMAL.
    bool UploadMips(ImageHandle handle, const void* data, VkDeviceSize size,
                    std::span<const MipUploadRegion> regions);

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] VkImage GetImage(ImageHandle handle) const;
    [[nodiscard]] VkImageView GetView(ImageHandle handle) const;
    [[nodiscard]] VkFormat GetFormat(ImageHandle handle) const;
    [[nodiscard]] VkExtent2D GetExtent(ImageHandle handle) const;
    [[nodiscard]] uint32_t GetMipLevels(ImageHandle handle) const;

private:
    struct ImageEntry
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkExtent2D Extent{};
        VkImageAspectFlags AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t MipLevels = 1;
        bool GenerateMips = false;
        uint32_t Generation = 0;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VmaAllocator Allocator = VK_NULL_HANDLE;
    VulkanUploadContextService* UploadCtx = nullptr;
    VulkanDeletionQueueService* DeletionQueue = nullptr;
    bool Valid = false;

    std::vector<ImageEntry> Entries;
    std::vector<uint32_t> FreeSlots;

    [[nodiscard]] ImageEntry* Resolve(ImageHandle handle);
    [[nodiscard]] const ImageEntry* Resolve(ImageHandle handle) const;
    [[nodiscard]] ImageHandle MakeHandle(uint32_t index, uint32_t generation) const;

    [[nodiscard]] bool CreateDefaultView(ImageEntry& entry);
    void RecordMipChain(VkCommandBuffer cmd, ImageEntry& entry);
};
