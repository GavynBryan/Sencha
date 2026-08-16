#include <graphics/vulkan/VulkanImageUploadValidation.h>

#include <format>
#include <vector>

bool DescribeUploadFormat(VkFormat format,
                          uint32_t& blockWidth,
                          uint32_t& blockHeight,
                          uint32_t& blockByteSize)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM:
        blockWidth = 1; blockHeight = 1; blockByteSize = 1;
        return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        blockWidth = 1; blockHeight = 1; blockByteSize = 4;
        return true;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        blockWidth = 1; blockHeight = 1; blockByteSize = 8;
        return true;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        blockWidth = 4; blockHeight = 4; blockByteSize = 8;
        return true;
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        blockWidth = 4; blockHeight = 4; blockByteSize = 16;
        return true;
    default:
        return false;
    }
}

uint64_t TightCopyFootprint(VkFormat format, uint32_t width, uint32_t height, uint32_t depth)
{
    uint32_t blockWidth = 0;
    uint32_t blockHeight = 0;
    uint32_t blockByteSize = 0;
    if (!DescribeUploadFormat(format, blockWidth, blockHeight, blockByteSize))
        return 0;
    if (width == 0 || height == 0 || depth == 0)
        return 0;

    const uint64_t blocksX = (uint64_t(width) + blockWidth - 1) / blockWidth;
    const uint64_t blocksY = (uint64_t(height) + blockHeight - 1) / blockHeight;
    return blocksX * blocksY * uint64_t(depth) * uint64_t(blockByteSize);
}

uint32_t MipExtent(uint32_t base, uint32_t mipLevel)
{
    if (mipLevel >= 32)
        return 1;
    const uint32_t shifted = base >> mipLevel;
    return shifted > 0 ? shifted : 1;
}

namespace
{
    // The copy, the optional mip blit, and every barrier on this path name the
    // color aspect explicitly. A depth or stencil image would record a copy the
    // barriers do not cover, so it is refused rather than partially handled.
    ImageUploadCheck CheckFormatAndAspect(const ImageUploadTarget& target, uint32_t& blockByteSize)
    {
        uint32_t blockWidth = 0;
        uint32_t blockHeight = 0;
        if (!DescribeUploadFormat(target.Format, blockWidth, blockHeight, blockByteSize))
            return ImageUploadCheck::Fail(
                std::format("upload: format {} has no known copy footprint",
                            static_cast<int>(target.Format)));

        if (target.AspectMask != VK_IMAGE_ASPECT_COLOR_BIT)
            return ImageUploadCheck::Fail("upload: only color-aspect images are supported");

        return ImageUploadCheck::Pass();
    }
} // namespace

ImageUploadCheck ValidateImageUpload(const ImageUploadTarget& target, VkDeviceSize size)
{
    if (size == 0)
        return ImageUploadCheck::Fail("Upload: empty staging data");

    // 3D images upload as one region spanning every depth slice (they are a
    // single array layer); cube/array images still have no upload path.
    const bool is3d = target.ViewType == VK_IMAGE_VIEW_TYPE_3D;
    if ((target.ViewType != VK_IMAGE_VIEW_TYPE_2D && !is3d) || target.ArrayLayers != 1)
        return ImageUploadCheck::Fail("Upload supports 2D and 3D single-layer images only");

    if (is3d && target.GenerateMips)
        return ImageUploadCheck::Fail("Upload: mip generation is not supported for 3D images");

    uint32_t baseBlockByteSize = 0;
    if (const ImageUploadCheck check = CheckFormatAndAspect(target, baseBlockByteSize); !check)
        return check;

    // The recorded copy spans the whole base mip, depth included. Vulkan reads
    // that many bytes whatever `size` says, so the staging buffer has to hold
    // them.
    const uint64_t footprint =
        TightCopyFootprint(target.Format, target.Extent.width, target.Extent.height, target.Depth);
    if (footprint == 0)
        return ImageUploadCheck::Fail("Upload: base mip has an empty extent");
    if (footprint > size)
        return ImageUploadCheck::Fail(
            std::format("Upload: staging holds {} bytes but the base-mip copy reads {}",
                        static_cast<uint64_t>(size), footprint));

    return ImageUploadCheck::Pass();
}

ImageUploadCheck ValidateImageMipUpload(const ImageUploadTarget& target,
                                        VkDeviceSize size,
                                        std::span<const ImageMipUploadRegion> regions)
{
    if (size == 0)
        return ImageUploadCheck::Fail("UploadMips: empty staging data");
    if (regions.empty())
        return ImageUploadCheck::Fail("UploadMips: no regions");

    if (target.ViewType != VK_IMAGE_VIEW_TYPE_2D || target.ArrayLayers != 1)
        return ImageUploadCheck::Fail("UploadMips supports 2D single-layer images only");

    if (target.GenerateMips)
        return ImageUploadCheck::Fail(
            "UploadMips: image was created with GenerateMips; cooked chains are explicit");

    uint32_t blockByteSize = 0;
    if (const ImageUploadCheck check = CheckFormatAndAspect(target, blockByteSize); !check)
        return check;

    // The image was created with an explicit level count and no mip
    // generation, so any level the regions do not write stays undefined memory
    // that the sampler will still read.
    if (regions.size() != target.MipLevels)
        return ImageUploadCheck::Fail(
            std::format("UploadMips: {} regions for an image with {} mip levels",
                        regions.size(), target.MipLevels));

    std::vector<bool> seen(target.MipLevels, false);
    for (const ImageMipUploadRegion& region : regions)
    {
        if (region.MipLevel >= target.MipLevels)
            return ImageUploadCheck::Fail(
                std::format("UploadMips: region mip {} out of range (image has {})",
                            region.MipLevel, target.MipLevels));
        if (seen[region.MipLevel])
            return ImageUploadCheck::Fail(
                std::format("UploadMips: mip {} appears more than once", region.MipLevel));
        seen[region.MipLevel] = true;

        const uint32_t expectedWidth = MipExtent(target.Extent.width, region.MipLevel);
        const uint32_t expectedHeight = MipExtent(target.Extent.height, region.MipLevel);
        if (region.Width != expectedWidth || region.Height != expectedHeight)
            return ImageUploadCheck::Fail(
                std::format("UploadMips: mip {} is {}x{} but the image's level is {}x{}",
                            region.MipLevel, region.Width, region.Height,
                            expectedWidth, expectedHeight));

        if (region.Offset >= size)
            return ImageUploadCheck::Fail(
                std::format("UploadMips: region offset {} beyond blob size {}",
                            static_cast<uint64_t>(region.Offset),
                            static_cast<uint64_t>(size)));

        // vkCmdCopyBufferToImage requires the buffer offset to be a multiple of
        // the texel block size.
        if (static_cast<uint64_t>(region.Offset) % blockByteSize != 0)
            return ImageUploadCheck::Fail(
                std::format("UploadMips: mip {} offset {} is not a multiple of the {}-byte block",
                            region.MipLevel, static_cast<uint64_t>(region.Offset), blockByteSize));

        const uint64_t footprint =
            TightCopyFootprint(target.Format, region.Width, region.Height, 1);
        if (footprint == 0)
            return ImageUploadCheck::Fail(
                std::format("UploadMips: mip {} has an empty extent", region.MipLevel));

        // Subtraction, not addition: offset is already known to be below size,
        // so this cannot wrap the way offset + footprint would.
        if (footprint > size - static_cast<uint64_t>(region.Offset))
            return ImageUploadCheck::Fail(
                std::format("UploadMips: mip {} reads {} bytes at offset {}, past the {}-byte blob",
                            region.MipLevel, footprint,
                            static_cast<uint64_t>(region.Offset),
                            static_cast<uint64_t>(size)));
    }

    return ImageUploadCheck::Pass();
}
