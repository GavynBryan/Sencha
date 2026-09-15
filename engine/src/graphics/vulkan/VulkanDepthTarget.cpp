#include <graphics/vulkan/VulkanDepthTarget.h>

#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

VulkanDepthTarget::VulkanDepthTarget(VulkanImageService& images,
                                     VulkanPhysicalDeviceService& physicalDevice)
    : Images(&images)
    , PhysicalDevice(physicalDevice.GetPhysicalDevice())
{
}

VulkanDepthTarget::~VulkanDepthTarget()
{
    Destroy();
}

void VulkanDepthTarget::Create(VkExtent2D extent)
{
    if (Images == nullptr || extent.width == 0 || extent.height == 0) return;

    Format = ChooseDepthFormat();
    if (Format == VK_FORMAT_UNDEFINED) return;

    ImageCreateInfo info{};
    info.Format = Format;
    info.Extent = extent;
    info.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    // Both aspects when the format carries a stencil. Vulkan requires the depth
    // and stencil attachments of one rendering scope to be the *same* view, so a
    // separate stencil-only view would be illegal rather than merely wasteful.
    info.AspectMask = HasStencil()
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
    info.DebugName = "Main depth target";

    Handle = Images->Create(info);
    if (Handle.IsValid())
    {
        Extent = extent;
    }
}

void VulkanDepthTarget::Destroy()
{
    if (Images != nullptr && Handle.IsValid())
    {
        Images->Destroy(Handle);
    }
    Handle = {};
    Extent = {};
}

void VulkanDepthTarget::Recreate(VkExtent2D extent)
{
    if (Handle.IsValid() && Extent.width == extent.width && Extent.height == extent.height)
    {
        return;
    }

    Destroy();
    Create(extent);
}

VkImage VulkanDepthTarget::GetImage() const
{
    return Images != nullptr ? Images->GetImage(Handle) : VK_NULL_HANDLE;
}

VkImageView VulkanDepthTarget::GetView() const
{
    return Images != nullptr ? Images->GetView(Handle) : VK_NULL_HANDLE;
}

VkFormat VulkanDepthTarget::ChooseDepthFormat() const
{
    // 32-bit float depth first, with a stencil aspect if the device offers it.
    //
    // Authored UI clips to rounded boundaries through a stencil-backed clip
    // mask, which a rectangular scissor cannot express -- and rounded panels are
    // most of what authored chrome is made of. So a stencil aspect is worth the
    // bandwidth where it is free, and D32_SFLOAT stays ahead of D24 either way
    // because depth precision is not a thing to trade for it.
    //
    // A device offering no stencil-bearing depth format still runs: the UI pass
    // degrades border-radius clipping to rectangular scissor and says so once.
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format, &props);
        if ((props.optimalTilingFeatures
             & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }

    return VK_FORMAT_UNDEFINED;
}
