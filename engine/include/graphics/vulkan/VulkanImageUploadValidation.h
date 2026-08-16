#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>

//=============================================================================
// Vulkan image upload validation
//
// The copy contract for VulkanImageService::Upload and ::UploadMips, split out
// so it can be checked without a device.
//
// vkCmdCopyBufferToImage derives how many bytes it reads from the image format
// and the copy extent, never from a size the caller supplies. A staging buffer
// that is shorter than that footprint is a GPU-side out-of-bounds read that no
// host-side length check would catch, so the service validates the footprint it
// is about to record against the staging size it is about to allocate.
//
// Nothing here touches a VkDevice or a live image: the target is described by
// value, which is what lets the boundary keep regression coverage in a
// repository with no device-backed test harness.
//=============================================================================

// One mip's extent and byte offset within a packed upload blob.
//
// Deliberately carries no byte size. A caller-authored length would only say
// what the caller believed; the footprint Vulkan actually reads is recomputed
// here from the format and the extent.
struct ImageMipUploadRegion
{
    uint32_t MipLevel = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkDeviceSize Offset = 0;
};

// The destination image as the copy contract sees it: the fields
// VulkanImageService keeps per image, minus the device-owned handles.
struct ImageUploadTarget
{
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
    uint32_t Depth = 1;
    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
    VkImageViewType ViewType = VK_IMAGE_VIEW_TYPE_2D;
    VkImageAspectFlags AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bool GenerateMips = false;
};

// Rejection carries its reason so the service logs the same diagnostics it did
// when these checks were inline.
struct ImageUploadCheck
{
    bool Ok = false;
    std::string Error;

    [[nodiscard]] explicit operator bool() const { return Ok; }

    static ImageUploadCheck Pass() { return ImageUploadCheck{ true, {} }; }
    static ImageUploadCheck Fail(std::string reason)
    {
        return ImageUploadCheck{ false, std::move(reason) };
    }
};

// Texel-block dimensions and size for the formats the upload path supports.
// Returns false for any other format, which is the validator declining to
// vouch for a footprint it cannot compute.
[[nodiscard]] bool DescribeUploadFormat(VkFormat format,
                                        uint32_t& blockWidth,
                                        uint32_t& blockHeight,
                                        uint32_t& blockByteSize);

// Bytes vkCmdCopyBufferToImage reads for a tightly packed copy of the given
// extent. Zero if the format is not one this path understands.
[[nodiscard]] uint64_t TightCopyFootprint(VkFormat format,
                                          uint32_t width,
                                          uint32_t height,
                                          uint32_t depth);

// Extent of `mipLevel` in an image whose base extent is `baseWidth` x
// `baseHeight`, using Vulkan's floor-halving convention.
[[nodiscard]] uint32_t MipExtent(uint32_t base, uint32_t mipLevel);

// Base-mip upload: `size` must cover the full base extent, depth included.
[[nodiscard]] ImageUploadCheck ValidateImageUpload(const ImageUploadTarget& target,
                                                   VkDeviceSize size);

// Packed mip-chain upload: every mip of the image must appear exactly once,
// at its own extent, inside the blob.
[[nodiscard]] ImageUploadCheck ValidateImageMipUpload(
    const ImageUploadTarget& target,
    VkDeviceSize size,
    std::span<const ImageMipUploadRegion> regions);
