#include <render/backend/vulkan/VulkanFrameScratch.h>

#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>

namespace
{
    VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        if (alignment <= 1) return value;
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

VulkanFrameScratch::VulkanFrameScratch(LoggingProvider& logging,
                                       VulkanDeviceService& device,
                                       VulkanPhysicalDeviceService& physicalDevice,
                                       VulkanBufferService& buffers,
                                       VulkanFrameScratch::Config config)
    : Log(logging.GetLogger<VulkanFrameScratch>())
    , Buffers(&buffers)
{
    if (!device.IsValid() || !physicalDevice.IsValid() || !buffers.IsValid())
    {
        Log.Error("Cannot create VulkanFrameScratch: upstream services not valid");
        return;
    }

    if (config.FramesInFlight == 0 || config.BytesPerFrame == 0)
    {
        Log.Error("VulkanFrameScratch Config must specify nonzero FramesInFlight and BytesPerFrame");
        return;
    }

    UniformAlignment = physicalDevice.GetProperties().limits.minUniformBufferOffsetAlignment;
    if (UniformAlignment == 0) UniformAlignment = 1;

    // Pad the per-frame slice up to the UBO alignment so slice boundaries
    // themselves land on a legal dynamic-offset boundary.
    BytesPerFrame = AlignUp(config.BytesPerFrame, UniformAlignment);
    FramesInFlight = config.FramesInFlight;

    BufferCreateInfo info{};
    info.Size = BytesPerFrame * FramesInFlight;
    info.Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
               | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
               | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.Memory = BufferMemory::HostVisible;
    info.DebugName = "VulkanFrameScratch.Ring";

    RingBuffer = buffers.Create(info);
    if (!RingBuffer.IsValid())
    {
        Log.Error("VulkanFrameScratch: failed to create ring buffer");
        return;
    }

    MappedBase = buffers.GetMappedPointer(RingBuffer);
    if (MappedBase == nullptr)
    {
        Log.Error("VulkanFrameScratch: ring buffer has no host mapping");
        buffers.Destroy(RingBuffer);
        RingBuffer = {};
        return;
    }

    // Start on frame 0 with a clean cursor.
    CurrentFrame = 0;
    Cursor = 0;
    Valid = true;
}

VulkanFrameScratch::~VulkanFrameScratch()
{
    if (RingBuffer.IsValid() && Buffers != nullptr)
    {
        Buffers->Destroy(RingBuffer);
    }
}

void VulkanFrameScratch::BeginFrame()
{
    if (!Valid) return;
    CurrentFrame = (CurrentFrame + 1) % FramesInFlight;
    Cursor = 0;
}

VulkanFrameScratch::Allocation VulkanFrameScratch::Allocate(VkDeviceSize size, VkDeviceSize alignment)
{
    if (!Valid || size == 0) return {};

    const VkDeviceSize alignedCursor = AlignUp(Cursor, alignment == 0 ? 1 : alignment);
    if (alignedCursor + size > BytesPerFrame)
    {
        Log.Error("VulkanFrameScratch: allocation of {} bytes exceeds frame slice capacity ({})",
                  static_cast<uint64_t>(size),
                  static_cast<uint64_t>(BytesPerFrame));
        return {};
    }

    const VkDeviceSize globalOffset = static_cast<VkDeviceSize>(CurrentFrame) * BytesPerFrame + alignedCursor;
    Cursor = alignedCursor + size;

    Allocation out;
    out.Buffer = RingBuffer;
    out.Offset = globalOffset;
    out.Mapped = static_cast<uint8_t*>(MappedBase) + globalOffset;
    return out;
}

VulkanFrameScratch::Allocation VulkanFrameScratch::AllocateUniform(VkDeviceSize size)
{
    return Allocate(size, UniformAlignment);
}

VulkanFrameScratch::Allocation VulkanFrameScratch::AllocateVertex(VkDeviceSize size)
{
    return Allocate(size, 16);
}
