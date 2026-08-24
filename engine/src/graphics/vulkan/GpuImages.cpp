#include <graphics/GpuImages.h>

#include <graphics/vulkan/VulkanImageService.h>

namespace
{
[[nodiscard]] VkFormat ToVulkanFormat(GpuFormat format)
{
    switch (format)
    {
    case GpuFormat::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkImageViewType ToVulkanViewType(GpuImageViewKind kind)
{
    switch (kind)
    {
    case GpuImageViewKind::Image2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case GpuImageViewKind::Volume:
        return VK_IMAGE_VIEW_TYPE_3D;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}
} // namespace

ImageHandle GpuImages::Create(const ImageDesc& desc)
{
    if (Impl == nullptr)
        return {};
    // TRANSFER_DST rides along unconditionally: every image the neutral
    // surface creates is uploadable by contract, and the bit is free.
    return Impl->Create(ImageCreateInfo{
        .Format = ToVulkanFormat(desc.Format),
        .Extent = { desc.Extent.Width, desc.Extent.Height },
        .Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .ViewType = ToVulkanViewType(desc.ViewKind),
        .Depth = desc.Depth,
        .DebugName = desc.DebugName,
    });
}

bool GpuImages::Upload(ImageHandle handle, const void* data, std::uint64_t size)
{
    if (Impl == nullptr)
        return false;
    return Impl->Upload(handle, data, size);
}

void GpuImages::Destroy(ImageHandle handle)
{
    if (Impl != nullptr)
        Impl->Destroy(handle);
}
