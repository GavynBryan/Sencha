#pragma once

#include <graphics/vulkan/VulkanImageService.h>
#include <vulkan/vulkan.h>

class VulkanImageService;
class VulkanPhysicalDeviceService;

//=============================================================================
// VulkanDepthTarget
//
// Owns a depth image and its view for use as the depth attachment in the main
// color pass. The format is selected at Create() time by probing the physical
// device; D32_SFLOAT is preferred, with D24/D32 stencil variants as fallbacks.
//
// Recreate() is a no-op when the extent hasn't changed, making it safe to call
// every frame before recording the render pass.
//=============================================================================
class VulkanDepthTarget
{
public:
    VulkanDepthTarget(VulkanImageService& images, VulkanPhysicalDeviceService& physicalDevice);
    ~VulkanDepthTarget();

    VulkanDepthTarget(const VulkanDepthTarget&) = delete;
    VulkanDepthTarget& operator=(const VulkanDepthTarget&) = delete;
    VulkanDepthTarget(VulkanDepthTarget&&) = delete;
    VulkanDepthTarget& operator=(VulkanDepthTarget&&) = delete;

    void Create(VkExtent2D extent);
    void Destroy();
    void Recreate(VkExtent2D extent);

    [[nodiscard]] VkImage GetImage() const;
    [[nodiscard]] VkImageView GetView() const;
    [[nodiscard]] VkFormat GetFormat() const { return Format; }

    // Whether the chosen format carries a stencil aspect. Authored UI clips to
    // rounded boundaries through it; a device that offered no stencil-bearing
    // depth format still runs, with that clipping degraded to a rectangle.
    [[nodiscard]] bool HasStencil() const
    {
        return Format == VK_FORMAT_D32_SFLOAT_S8_UINT
            || Format == VK_FORMAT_D24_UNORM_S8_UINT
            || Format == VK_FORMAT_D16_UNORM_S8_UINT;
    }
    [[nodiscard]] VkExtent2D GetExtent() const { return Extent; }

private:
    VulkanImageService* Images = nullptr;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    ImageHandle Handle{};
    VkExtent2D Extent{};
    VkFormat Format = VK_FORMAT_UNDEFINED;

    [[nodiscard]] VkFormat ChooseDepthFormat() const;
};
