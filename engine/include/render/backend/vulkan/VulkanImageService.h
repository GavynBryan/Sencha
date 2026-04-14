#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanAllocatorService;
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
// Cubemaps, 3D textures, and non-default views are out of scope until a
// feature actually needs them.
//
// Upload() is synchronous, blocking on a fence, and transitions the image
// to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when it returns. The bindless
// texture array in the descriptor cache (step 7) expects every bound image
// to sit in that layout.
//
// Upload runs on the graphics queue to avoid queue-family ownership
// transfers. Same rationale as VulkanBufferService.
//=============================================================================
struct ImageHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const ImageHandle&) const = default;
};

struct ImageCreateInfo
{
    VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;
    VkExtent2D Extent{};
    VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
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
                       VulkanUploadContextService& upload);
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
        uint32_t MipLevels = 1;
        bool GenerateMips = false;
        uint32_t Generation = 0;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VmaAllocator Allocator = VK_NULL_HANDLE;
    VulkanUploadContextService* UploadCtx = nullptr;
    bool Valid = false;

    std::vector<ImageEntry> Entries;
    std::vector<uint32_t> FreeSlots;

    [[nodiscard]] ImageEntry* Resolve(ImageHandle handle);
    [[nodiscard]] const ImageEntry* Resolve(ImageHandle handle) const;
    [[nodiscard]] ImageHandle MakeHandle(uint32_t index, uint32_t generation) const;

    [[nodiscard]] bool CreateDefaultView(ImageEntry& entry);
    void RecordMipChain(VkCommandBuffer cmd, ImageEntry& entry);
};
