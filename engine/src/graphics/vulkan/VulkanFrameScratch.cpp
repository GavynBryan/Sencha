#include <graphics/vulkan/VulkanFrameScratch.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

#include <algorithm>
#include <limits>

namespace
{
    VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        if (alignment <= 1) return value;
        if (value > std::numeric_limits<VkDeviceSize>::max() - (alignment - 1))
            return std::numeric_limits<VkDeviceSize>::max();
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
    Ring = FrameScratchRing(config.FramesInFlight,
                            AlignUp(config.BytesPerFrame, UniformAlignment));

    BufferCreateInfo info{};
    info.Size = Ring.GetTotalBytes();
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
    Ring.BeginFrame();
}

VulkanFrameScratch::Allocation VulkanFrameScratch::Allocate(VkDeviceSize size, VkDeviceSize alignment)
{
    if (!Valid) return {};

    const FrameScratchRing::Grant grant =
        Ring.Allocate(size, alignment == 0 ? 1 : alignment);
    if (!grant.IsValid())
    {
        if (size != 0)
        {
            Log.Error("VulkanFrameScratch: allocation of {} bytes at cursor {} exceeds "
                      "frame slice capacity ({})",
                      static_cast<uint64_t>(size),
                      static_cast<uint64_t>(Ring.GetUsedBytes()),
                      static_cast<uint64_t>(Ring.GetBytesPerFrame()));
        }
        return {};
    }
    return MakeAllocation(grant);
}

VulkanFrameScratch::Allocation VulkanFrameScratch::MakeAllocation(
    const FrameScratchRing::Grant& grant) const
{
    Allocation out;
    out.Buffer = RingBuffer;
    out.Offset = grant.Offset;
    out.Mapped = static_cast<uint8_t*>(MappedBase) + grant.Offset;
    return out;
}

VulkanFrameScratch::Allocation VulkanFrameScratch::AllocateUniform(VkDeviceSize size)
{
    return Allocate(size, UniformAlignment);
}

VulkanFrameScratch::Allocation VulkanFrameScratch::AllocateVertex(VkDeviceSize size)
{
    return Allocate(size, kVertexAlignment);
}

VulkanFrameScratch::ElementAllocation VulkanFrameScratch::AllocateVertexElements(
    uint32_t maxElements, VkDeviceSize stride)
{
    if (!Valid) return {};

    const FrameScratchRing::Grant grant =
        Ring.AllocateElements(maxElements, stride, kVertexAlignment);
    if (!grant.IsValid())
    {
        if (maxElements != 0 && stride != 0)
        {
            Log.Error("VulkanFrameScratch: no room for a {}-byte element at cursor {} "
                      "of frame slice capacity ({})",
                      static_cast<uint64_t>(stride),
                      static_cast<uint64_t>(Ring.GetUsedBytes()),
                      static_cast<uint64_t>(Ring.GetBytesPerFrame()));
        }
        return {};
    }

    ElementAllocation out;
    out.Grant = MakeAllocation(grant);
    out.Count = static_cast<uint32_t>(grant.Bytes / stride);
    return out;
}
