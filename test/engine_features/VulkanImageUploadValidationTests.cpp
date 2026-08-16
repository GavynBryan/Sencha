#ifdef SENCHA_ENABLE_VULKAN

#include <graphics/vulkan/VulkanImageUploadValidation.h>

#include <gtest/gtest.h>

#include <vector>

// The copy contract for VulkanImageService's two upload entry points. The
// service records copies whose byte footprint Vulkan derives from the format
// and extent, so an undersized staging buffer is a GPU-side out-of-bounds read
// that no host-side length check can catch. There is no device-backed test
// harness in this repository, which is the whole reason these checks live in a
// pure mechanism that can be exercised here.

namespace
{
    ImageUploadTarget Rgba8Target(uint32_t width, uint32_t height, uint32_t mips = 1)
    {
        ImageUploadTarget target;
        target.Format = VK_FORMAT_R8G8B8A8_SRGB;
        target.Extent = { width, height };
        target.MipLevels = mips;
        return target;
    }

    ImageUploadTarget Bc7Target(uint32_t width, uint32_t height, uint32_t mips)
    {
        ImageUploadTarget target;
        target.Format = VK_FORMAT_BC7_SRGB_BLOCK;
        target.Extent = { width, height };
        target.MipLevels = mips;
        return target;
    }

    // The region layout TextureCache builds from a cooked mip table: one entry
    // per level, tightly packed, in level order.
    std::vector<ImageMipUploadRegion> PackedChain(const ImageUploadTarget& target,
                                                  VkDeviceSize& totalOut)
    {
        std::vector<ImageMipUploadRegion> regions;
        VkDeviceSize offset = 0;
        for (uint32_t level = 0; level < target.MipLevels; ++level)
        {
            const uint32_t w = MipExtent(target.Extent.width, level);
            const uint32_t h = MipExtent(target.Extent.height, level);
            regions.push_back(ImageMipUploadRegion{ level, w, h, offset });
            offset += TightCopyFootprint(target.Format, w, h, 1);
        }
        totalOut = offset;
        return regions;
    }
} // namespace

// -- Footprint math --------------------------------------------------------

TEST(VulkanImageUploadValidation, FootprintFollowsTheFormatBlockGeometry)
{
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_R8G8B8A8_SRGB, 4, 4, 1), 64u);
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_R8_UNORM, 8, 8, 1), 64u);
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_R16G16B16A16_SFLOAT, 4, 4, 4), 512u);

    // Block formats round up to whole blocks, so every mip below 4x4 still
    // costs one block.
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_BC7_SRGB_BLOCK, 8, 8, 1), 64u);
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_BC7_SRGB_BLOCK, 2, 2, 1), 16u);
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_BC4_UNORM_BLOCK, 4, 4, 1), 8u);

    // A format the validator cannot size must not report a footprint.
    EXPECT_EQ(TightCopyFootprint(VK_FORMAT_D16_UNORM, 4, 4, 1), 0u);
}

TEST(VulkanImageUploadValidation, MipExtentFloorHalvesAndClampsAtOne)
{
    EXPECT_EQ(MipExtent(8, 0), 8u);
    EXPECT_EQ(MipExtent(8, 3), 1u);
    EXPECT_EQ(MipExtent(8, 9), 1u);
    EXPECT_EQ(MipExtent(7, 1), 3u);
}

// -- Base-mip upload -------------------------------------------------------

TEST(VulkanImageUploadValidation, AcceptsBaseUploadCoveringTheFullExtent)
{
    EXPECT_TRUE(ValidateImageUpload(Rgba8Target(4, 4), 64).Ok);
}

TEST(VulkanImageUploadValidation, RejectsBaseUploadShorterThanTheCopyFootprint)
{
    const ImageUploadCheck check = ValidateImageUpload(Rgba8Target(4, 4), 32);
    EXPECT_FALSE(check.Ok) << "a 4x4 RGBA8 copy reads 64 bytes regardless of the size passed";
    EXPECT_FALSE(check.Error.empty());
}

TEST(VulkanImageUploadValidation, RejectsBaseUploadThatIgnoresDepth)
{
    ImageUploadTarget volume;
    volume.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
    volume.Extent = { 4, 4 };
    volume.Depth = 4;
    volume.ViewType = VK_IMAGE_VIEW_TYPE_3D;

    EXPECT_TRUE(ValidateImageUpload(volume, 512).Ok);
    EXPECT_FALSE(ValidateImageUpload(volume, 128).Ok)
        << "a 3D copy spans every depth slice, not just one";
}

TEST(VulkanImageUploadValidation, RejectsTargetsTheUploadPathDoesNotSupport)
{
    ImageUploadTarget cube = Rgba8Target(4, 4);
    cube.ViewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cube.ArrayLayers = 6;
    EXPECT_FALSE(ValidateImageUpload(cube, 64 * 6).Ok);

    ImageUploadTarget volumeWithMipGen;
    volumeWithMipGen.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
    volumeWithMipGen.Extent = { 4, 4 };
    volumeWithMipGen.Depth = 4;
    volumeWithMipGen.ViewType = VK_IMAGE_VIEW_TYPE_3D;
    volumeWithMipGen.GenerateMips = true;
    EXPECT_FALSE(ValidateImageUpload(volumeWithMipGen, 512).Ok);
}

TEST(VulkanImageUploadValidation, RejectsFormatsAndAspectsTheCopyPathCannotHonour)
{
    ImageUploadTarget unsized = Rgba8Target(4, 4);
    unsized.Format = VK_FORMAT_D16_UNORM;
    EXPECT_FALSE(ValidateImageUpload(unsized, 4096).Ok)
        << "the validator must not vouch for a footprint it cannot compute";

    // The copy, the optional blit, and every barrier on this path are written
    // for the color aspect. Accepting a depth image here would record a copy
    // the barriers do not cover.
    ImageUploadTarget depthAspect = Rgba8Target(4, 4);
    depthAspect.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    EXPECT_FALSE(ValidateImageUpload(depthAspect, 64).Ok);
}

// -- Packed mip-chain upload -----------------------------------------------

TEST(VulkanImageUploadValidation, AcceptsTheCookedPackedChainLayout)
{
    const ImageUploadTarget target = Bc7Target(8, 8, 4);
    VkDeviceSize total = 0;
    const std::vector<ImageMipUploadRegion> regions = PackedChain(target, total);

    ASSERT_EQ(regions.size(), 4u);
    EXPECT_EQ(total, 64u + 16u + 16u + 16u);
    EXPECT_TRUE(ValidateImageMipUpload(target, total, regions).Ok);
}

TEST(VulkanImageUploadValidation, RejectsRegionWhoseFootprintRunsPastTheBlob)
{
    const ImageUploadTarget target = Bc7Target(8, 8, 4);
    VkDeviceSize total = 0;
    std::vector<ImageMipUploadRegion> regions = PackedChain(target, total);

    // Every offset still lands inside the blob, so an offset-only bound passes
    // while the last copy reads past the end.
    EXPECT_FALSE(ValidateImageMipUpload(target, total - 1, regions).Ok);
}

TEST(VulkanImageUploadValidation, RejectsRegionOffsetThatWouldOverflowTheEndBound)
{
    const ImageUploadTarget target = Rgba8Target(4, 4, 1);
    std::vector<ImageMipUploadRegion> regions{
        ImageMipUploadRegion{ 0, 4, 4, ~VkDeviceSize(0) - 8 },
    };
    EXPECT_FALSE(ValidateImageMipUpload(target, 64, regions).Ok);
}

TEST(VulkanImageUploadValidation, RejectsRegionExtentThatIsNotItsMipExtent)
{
    const ImageUploadTarget target = Bc7Target(8, 8, 4);
    VkDeviceSize total = 0;
    std::vector<ImageMipUploadRegion> regions = PackedChain(target, total);

    regions[1].Width = 8; // mip 1 of an 8x8 image is 4x4
    EXPECT_FALSE(ValidateImageMipUpload(target, total, regions).Ok);
}

TEST(VulkanImageUploadValidation, RejectsDuplicateAndMissingMips)
{
    const ImageUploadTarget target = Bc7Target(8, 8, 4);
    VkDeviceSize total = 0;
    const std::vector<ImageMipUploadRegion> packed = PackedChain(target, total);

    std::vector<ImageMipUploadRegion> duplicated = packed;
    duplicated[2] = duplicated[1];
    EXPECT_FALSE(ValidateImageMipUpload(target, total, duplicated).Ok)
        << "a duplicated level leaves another level uninitialized";

    std::vector<ImageMipUploadRegion> missing = packed;
    missing.pop_back();
    EXPECT_FALSE(ValidateImageMipUpload(target, total, missing).Ok)
        << "the image declares four mips; sampling an unwritten one reads undefined memory";
}

TEST(VulkanImageUploadValidation, RejectsOffsetsThatBreakBlockAlignment)
{
    const ImageUploadTarget target = Bc7Target(8, 8, 1);
    std::vector<ImageMipUploadRegion> regions{
        ImageMipUploadRegion{ 0, 8, 8, 8 }, // BC7 blocks are 16 bytes
    };
    EXPECT_FALSE(ValidateImageMipUpload(target, 8 + 64, regions).Ok);
}

TEST(VulkanImageUploadValidation, PreservesTheExistingRegionRejections)
{
    const ImageUploadTarget target = Rgba8Target(4, 4, 1);

    std::vector<ImageMipUploadRegion> outOfRange{ ImageMipUploadRegion{ 3, 4, 4, 0 } };
    EXPECT_FALSE(ValidateImageMipUpload(target, 64, outOfRange).Ok);

    std::vector<ImageMipUploadRegion> pastEnd{ ImageMipUploadRegion{ 0, 4, 4, 64 } };
    EXPECT_FALSE(ValidateImageMipUpload(target, 64, pastEnd).Ok);

    EXPECT_FALSE(ValidateImageMipUpload(target, 64, {}).Ok);
    EXPECT_FALSE(ValidateImageMipUpload(target, 0, outOfRange).Ok);

    ImageUploadTarget generated = target;
    generated.GenerateMips = true;
    std::vector<ImageMipUploadRegion> ok{ ImageMipUploadRegion{ 0, 4, 4, 0 } };
    EXPECT_FALSE(ValidateImageMipUpload(generated, 64, ok).Ok);

    ImageUploadTarget cube = target;
    cube.ViewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cube.ArrayLayers = 6;
    EXPECT_FALSE(ValidateImageMipUpload(cube, 64, ok).Ok);
}

#endif // SENCHA_ENABLE_VULKAN
