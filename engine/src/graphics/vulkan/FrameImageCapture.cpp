#include <graphics/vulkan/FrameImageCapture.h>

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

// The only translation unit that needs the writer, so it carries the
// implementation, matching how ImageLoader.cpp carries the reader's.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <vector>

namespace
{

// Host-visible and coherent, so the write path is a map and a read with no
// explicit invalidate. Capture is a dev facility; the simple memory type is
// worth more than the fastest one.
[[nodiscard]] std::uint32_t FindHostVisibleMemoryType(VkPhysicalDevice physicalDevice,
                                                      std::uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    constexpr VkMemoryPropertyFlags wanted =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        const bool usable = (typeBits & (1u << index)) != 0;
        if (usable && (properties.memoryTypes[index].propertyFlags & wanted) == wanted)
            return index;
    }
    return UINT32_MAX;
}

// The swapchain is normally B8G8R8A8; PNG wants RGBA byte order.
[[nodiscard]] bool FormatIsBlueFirst(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

} // namespace

void FrameImageCapture::Setup(const RendererServices& services)
{
    Logging = services.Logging;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    PhysicalDevice = services.PhysicalDevice != nullptr
        ? services.PhysicalDevice->GetPhysicalDevice()
        : VK_NULL_HANDLE;
}

void FrameImageCapture::Release()
{
    if (Buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Buffer, nullptr);
    if (Memory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Memory, nullptr);
    Buffer = VK_NULL_HANDLE;
    Memory = VK_NULL_HANDLE;
    Size = 0;
    Width = 0;
    Height = 0;
    WritePath.clear();
}

void FrameImageCapture::Teardown()
{
    // ~Renderer has already waited the device out, so anything still pending is
    // readable now and would otherwise be silently dropped -- which is what a
    // capture armed for the last frame of a bounded run always is.
    if (Buffer != VK_NULL_HANDLE && !WritePath.empty())
        Write();
    Release();
    PendingPath.clear();
    Device = VK_NULL_HANDLE;
    PhysicalDevice = VK_NULL_HANDLE;
}

void FrameImageCapture::Request(std::string path, std::uint64_t atFrame)
{
    PendingPath = std::move(path);
    PendingFrame = atFrame;
}

void FrameImageCapture::Record(const FrameContext& frame, std::uint64_t frameNumber,
                               VkImage image, VkExtent2D extent, VkFormat format)
{
    if (PendingPath.empty() || Device == VK_NULL_HANDLE)
        return;
    if (frameNumber < PendingFrame)
        return;
    if (extent.width == 0 || extent.height == 0)
        return;
    // One capture in flight: the previous one has not been written yet, so
    // hold the request rather than overwrite a buffer the GPU may still read.
    if (Buffer != VK_NULL_HANDLE)
        return;

    Logger* log = Logging != nullptr ? &Logging->GetLogger<FrameImageCapture>() : nullptr;

    const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Device, &bufferInfo, nullptr, &Buffer) != VK_SUCCESS)
    {
        if (log != nullptr) log->Error("capture: readback buffer creation failed");
        Release();
        PendingPath.clear();
        return;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(Device, Buffer, &requirements);
    const std::uint32_t typeIndex =
        FindHostVisibleMemoryType(PhysicalDevice, requirements.memoryTypeBits);
    if (typeIndex == UINT32_MAX)
    {
        if (log != nullptr) log->Error("capture: no host-visible memory type");
        Release();
        PendingPath.clear();
        return;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(Device, &allocateInfo, nullptr, &Memory) != VK_SUCCESS
        || vkBindBufferMemory(Device, Buffer, Memory, 0) != VK_SUCCESS)
    {
        if (log != nullptr) log->Error("capture: readback allocation failed");
        Release();
        PendingPath.clear();
        return;
    }

    // The image arrives as a colour attachment and has to leave as one: the
    // present transition after this reads COLOR_ATTACHMENT_OPTIMAL as the old
    // layout, and naming the wrong one there discards the frame's contents.
    VulkanBarriers::ImageTransition toSource{};
    toSource.Image = image;
    toSource.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSource.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSource.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toSource.DstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    toSource.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toSource.DstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
    toSource.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toSource);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { extent.width, extent.height, 1 };
    vkCmdCopyImageToBuffer(frame.Cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           Buffer, 1, &region);

    VulkanBarriers::ImageTransition back{};
    back.Image = image;
    back.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    back.SrcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    back.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    back.SrcAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
    back.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    back.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, back);

    Size = size;
    Width = extent.width;
    Height = extent.height;
    SwapRedBlue = FormatIsBlueFirst(format);
    WritePath = std::move(PendingPath);
    PendingPath.clear();
    PendingFrame = 0;
    Stamp = frame.Retirement.Stamp();
}

void FrameImageCapture::Drain(GpuFrameRetirement retirement)
{
    if (Buffer == VK_NULL_HANDLE || WritePath.empty())
        return;
    if (!retirement.IsRetired(Stamp))
        return;
    Write();
}

void FrameImageCapture::Write()
{
    Logger* log = Logging != nullptr ? &Logging->GetLogger<FrameImageCapture>() : nullptr;

    void* mapped = nullptr;
    if (vkMapMemory(Device, Memory, 0, Size, 0, &mapped) != VK_SUCCESS)
    {
        if (log != nullptr) log->Error("capture: mapping the readback buffer failed");
        Release();
        return;
    }

    // Copied rather than written in place: the mapping is the GPU's buffer, and
    // the channel swap would otherwise modify it.
    std::vector<unsigned char> pixels(static_cast<std::size_t>(Size));
    const auto* source = static_cast<const unsigned char*>(mapped);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i + 0] = SwapRedBlue ? source[i + 2] : source[i + 0];
        pixels[i + 1] = source[i + 1];
        pixels[i + 2] = SwapRedBlue ? source[i + 0] : source[i + 2];
        // Opaque: the swapchain's alpha is not meaningful, and leaving it as
        // written makes a comparison depend on what a pass happened to store.
        pixels[i + 3] = 255;
    }
    vkUnmapMemory(Device, Memory);

    const int written = stbi_write_png(WritePath.c_str(), static_cast<int>(Width),
                                       static_cast<int>(Height), 4, pixels.data(),
                                       static_cast<int>(Width) * 4);
    if (log != nullptr)
    {
        if (written != 0)
            log->Info("capture: wrote {}x{} to {}", Width, Height, WritePath);
        else
            log->Error("capture: writing {} failed", WritePath);
    }
    Release();
}
