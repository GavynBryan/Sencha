#include <graphics/vulkan/VulkanImageService.h>

#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>

#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint32_t kIndexBits = 20;
    constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGeneration = (1u << (32u - kIndexBits)) - 1u;

    uint32_t DecodeIndex(uint32_t id) { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }

    uint32_t FloorLog2(uint32_t v)
    {
        uint32_t r = 0;
        while (v >>= 1) ++r;
        return r;
    }

    uint32_t MaxMipLevels(VkExtent2D extent)
    {
        const uint32_t m = std::max(extent.width, extent.height);
        return m == 0 ? 1u : FloorLog2(m) + 1u;
    }
}

VulkanImageService::VulkanImageService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanAllocatorService& allocator,
    VulkanUploadContextService& upload,
    VulkanDeletionQueueService& deletionQueue)
    : Log(logging.GetLogger<VulkanImageService>())
    , Device(device.GetDevice())
    , Allocator(allocator.GetAllocator())
    , UploadCtx(&upload)
    , DeletionQueue(&deletionQueue)
{
    if (!device.IsValid() || !allocator.IsValid() || !upload.IsValid())
    {
        Log.Error("Cannot create VulkanImageService: upstream Vulkan services not valid");
        return;
    }

    Entries.emplace_back(); // reserve slot 0
    Valid = true;
}

VulkanImageService::~VulkanImageService()
{
    for (size_t i = 1; i < Entries.size(); ++i)
    {
        auto& entry = Entries[i];
        if (entry.View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Device, entry.View, nullptr);
            entry.View = VK_NULL_HANDLE;
        }
        if (entry.Image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(Allocator, entry.Image, entry.Allocation);
            entry.Image = VK_NULL_HANDLE;
            entry.Allocation = VK_NULL_HANDLE;
        }
    }
}

ImageHandle VulkanImageService::MakeHandle(uint32_t index, uint32_t generation) const
{
    ImageHandle h;
    h.Id = (generation << kIndexBits) | (index & kIndexMask);
    return h;
}

VulkanImageService::ImageEntry* VulkanImageService::Resolve(ImageHandle handle)
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Image == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

const VulkanImageService::ImageEntry* VulkanImageService::Resolve(ImageHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    const auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Image == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

ImageHandle VulkanImageService::Create(const ImageCreateInfo& info)
{
    if (info.Extent.width == 0 || info.Extent.height == 0)
    {
        Log.Error("ImageCreateInfo must specify nonzero extent");
        return {};
    }

    uint32_t mipLevels = info.MipLevels == 0 ? 1u : info.MipLevels;
    if (info.GenerateMips)
    {
        mipLevels = MaxMipLevels(info.Extent);
    }

    // Ensure usage is consistent with requested behavior.
    VkImageUsageFlags usage = info.Usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (info.GenerateMips)
    {
        // vkCmdBlitImage needs src + dst on the same image across mips.
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = info.Format;
    imageInfo.extent = { info.Extent.width, info.Extent.height, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    const VkResult result = vmaCreateImage(
        Allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        Log.Error("vmaCreateImage failed ({})", static_cast<int>(result));
        return {};
    }

    if (info.DebugName != nullptr)
    {
        vmaSetAllocationName(Allocator, allocation, info.DebugName);
    }

    uint32_t index;
    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Entries.size());
        if (index > kIndexMask)
        {
            Log.Error("VulkanImageService slot capacity exhausted");
            vmaDestroyImage(Allocator, image, allocation);
            return {};
        }
        Entries.emplace_back();
    }

    auto& entry = Entries[index];
    entry.Image = image;
    entry.Allocation = allocation;
    entry.Format = info.Format;
    entry.Extent = info.Extent;
    entry.AspectMask = info.AspectMask;
    entry.MipLevels = mipLevels;
    entry.GenerateMips = info.GenerateMips;
    entry.Generation = entry.Generation + 1;
    if (entry.Generation == 0 || entry.Generation > kMaxGeneration)
    {
        entry.Generation = 1;
    }

    if (!CreateDefaultView(entry))
    {
        vmaDestroyImage(Allocator, entry.Image, entry.Allocation);
        entry.Image = VK_NULL_HANDLE;
        entry.Allocation = VK_NULL_HANDLE;
        FreeSlots.push_back(index);
        return {};
    }

    return MakeHandle(index, entry.Generation);
}

bool VulkanImageService::CreateDefaultView(ImageEntry& entry)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = entry.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = entry.Format;
    viewInfo.subresourceRange.aspectMask = entry.AspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = entry.MipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    const VkResult result = vkCreateImageView(Device, &viewInfo, nullptr, &entry.View);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateImageView failed ({})", static_cast<int>(result));
        return false;
    }
    return true;
}

void VulkanImageService::Destroy(ImageHandle handle)
{
    auto* entry = Resolve(handle);
    if (entry == nullptr) return;

    // Snapshot VK handles before nulling the entry so the slot can be
    // reused immediately (Create bumps the generation on next alloc, making
    // any surviving ImageHandle copies fail Resolve).
    const VkImageView   view  = entry->View;
    const VkImage       image = entry->Image;
    const VmaAllocation alloc = entry->Allocation;

    entry->View       = VK_NULL_HANDLE;
    entry->Image      = VK_NULL_HANDLE;
    entry->Allocation = VK_NULL_HANDLE;

    const uint32_t index = DecodeIndex(handle.Id);
    FreeSlots.push_back(index);

    // Defer the physical destroy until the GPU has retired all command buffers
    // that could reference these handles.
    DeletionQueue->EnqueueImageDestroy({ Device, Allocator, view, image, alloc });
}

VkImage VulkanImageService::GetImage(ImageHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Image : VK_NULL_HANDLE;
}

VkImageView VulkanImageService::GetView(ImageHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->View : VK_NULL_HANDLE;
}

VkFormat VulkanImageService::GetFormat(ImageHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Format : VK_FORMAT_UNDEFINED;
}

VkExtent2D VulkanImageService::GetExtent(ImageHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Extent : VkExtent2D{};
}

uint32_t VulkanImageService::GetMipLevels(ImageHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->MipLevels : 0;
}

bool VulkanImageService::Upload(ImageHandle handle, const void* data, VkDeviceSize size)
{
    if (data == nullptr || size == 0) return false;

    auto* entry = Resolve(handle);
    if (entry == nullptr)
    {
        Log.Error("Upload: invalid ImageHandle");
        return false;
    }

    // Staging buffer.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingResult{};
    VkResult result = vmaCreateBuffer(
        Allocator, &bufferInfo, &allocInfo, &staging, &stagingAllocation, &stagingResult);
    if (result != VK_SUCCESS)
    {
        Log.Error("image upload: staging vmaCreateBuffer failed ({})", static_cast<int>(result));
        return false;
    }

    std::memcpy(stagingResult.pMappedData, data, static_cast<size_t>(size));
    vmaFlushAllocation(Allocator, stagingAllocation, 0, size);

    VkCommandBuffer cmd = UploadCtx->Begin();
    if (cmd == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(Allocator, staging, stagingAllocation);
        return false;
    }

    // UNDEFINED -> TRANSFER_DST across the whole mip chain (base + children).
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = entry->Image;
        t.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        t.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        t.BaseMipLevel = 0;
        t.LevelCount = entry->MipLevels;
        VulkanBarriers::TransitionImage(cmd, t);
    }

    // Copy staging into base mip.
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { entry->Extent.width, entry->Extent.height, 1 };

    vkCmdCopyBufferToImage(
        cmd, staging, entry->Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    if (entry->GenerateMips && entry->MipLevels > 1)
    {
        RecordMipChain(cmd, *entry);
    }
    else
    {
        // Transition the whole image (all mips) to SHADER_READ_ONLY.
        VulkanBarriers::ImageTransition t{};
        t.Image = entry->Image;
        t.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        t.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        t.SrcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        t.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        t.BaseMipLevel = 0;
        t.LevelCount = entry->MipLevels;
        VulkanBarriers::TransitionImage(cmd, t);
    }

    const bool ok = UploadCtx->Submit(cmd);
    vmaDestroyBuffer(Allocator, staging, stagingAllocation);
    return ok;
}

void VulkanImageService::RecordMipChain(VkCommandBuffer cmd, ImageEntry& entry)
{
    // Base mip is currently TRANSFER_DST (from the copy above). Transition it
    // to TRANSFER_SRC so it can serve as the blit source for mip 1.
    // Mips 1..N are still TRANSFER_DST from the initial whole-image transition.
    int32_t mipWidth = static_cast<int32_t>(entry.Extent.width);
    int32_t mipHeight = static_cast<int32_t>(entry.Extent.height);

    for (uint32_t i = 1; i < entry.MipLevels; ++i)
    {
        // mip (i-1): TRANSFER_DST -> TRANSFER_SRC
        {
            VulkanBarriers::ImageTransition t{};
            t.Image = entry.Image;
            t.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            t.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            t.SrcStage = VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
            t.DstStage = VK_PIPELINE_STAGE_2_BLIT_BIT;
            t.SrcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            t.DstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
            t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            t.BaseMipLevel = i - 1;
            t.LevelCount = 1;
            VulkanBarriers::TransitionImage(cmd, t);
        }

        const int32_t nextWidth = std::max(1, mipWidth / 2);
        const int32_t nextHeight = std::max(1, mipHeight / 2);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };

        vkCmdBlitImage(
            cmd,
            entry.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            entry.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // Final layout transition: everything -> SHADER_READ_ONLY.
    // Mips 0..N-2 are in TRANSFER_SRC, mip N-1 is in TRANSFER_DST.
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = entry.Image;
        t.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        t.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        t.SrcAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
        t.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        t.BaseMipLevel = 0;
        t.LevelCount = entry.MipLevels - 1;
        VulkanBarriers::TransitionImage(cmd, t);
    }
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = entry.Image;
        t.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        t.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        t.SrcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        t.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        t.BaseMipLevel = entry.MipLevels - 1;
        t.LevelCount = 1;
        VulkanBarriers::TransitionImage(cmd, t);
    }
}
