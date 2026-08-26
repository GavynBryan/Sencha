#include <graphics/GpuFrameScratch.h>

#include <graphics/vulkan/VulkanBufferService.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>

#include <algorithm>

GpuFrameScratch::GpuFrameScratch(LoggingProvider& logging,
                                       VulkanDeviceService& device,
                                       VulkanPhysicalDeviceService& physicalDevice,
                                       VulkanBufferService& buffers,
                                       GpuFrameScratch::Config config)
    : Log(logging.GetLogger<GpuFrameScratch>())
    , Buffers(&buffers)
{
    if (!device.IsValid() || !physicalDevice.IsValid() || !buffers.IsValid())
    {
        Log.Error("Cannot create GpuFrameScratch: upstream services not valid");
        return;
    }

    if (config.FramesInFlight == 0 || config.BytesPerFrame == 0)
    {
        Log.Error("GpuFrameScratch Config must specify nonzero FramesInFlight and BytesPerFrame");
        return;
    }

    UniformAlignment = physicalDevice.GetProperties().limits.minUniformBufferOffsetAlignment;
    if (UniformAlignment == 0) UniformAlignment = 1;

    // Pad the per-frame slice so slice boundaries themselves land on a legal
    // descriptor-offset boundary -- for storage bindings as well as uniform
    // ones, since the ring aligns cursors within a slice and the pose palettes
    // bind out of it as storage buffers.
    Ring = FrameScratchRing(
        config.FramesInFlight,
        ResolveScratchSliceBytes(config.BytesPerFrame, UniformAlignment));

    BufferCreateInfo info{};
    info.Size = Ring.GetTotalBytes();
    info.Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
               | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
               | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.Memory = BufferMemory::HostVisible;
    info.DebugName = "GpuFrameScratch.Ring";

    RingBuffer = buffers.Create(info);
    if (!RingBuffer.IsValid())
    {
        Log.Error("GpuFrameScratch: failed to create ring buffer");
        return;
    }

    MappedBase = buffers.GetMappedPointer(RingBuffer);
    if (MappedBase == nullptr)
    {
        Log.Error("GpuFrameScratch: ring buffer has no host mapping");
        buffers.Destroy(RingBuffer);
        RingBuffer = {};
        return;
    }

    Valid = true;
}

GpuFrameScratch::~GpuFrameScratch()
{
    if (RingBuffer.IsValid() && Buffers != nullptr)
    {
        Buffers->Destroy(RingBuffer);
    }
}

std::string_view ToString(ScratchTag tag)
{
    switch (tag)
    {
        case ScratchTag::SkinningPalettes:         return "skinning_palettes";
        case ScratchTag::ShadowViewUniforms:       return "shadow_view_uniforms";
        case ScratchTag::ShadowInstanceTransforms: return "shadow_instance_transforms";
        case ScratchTag::ForwardViewUniforms:      return "forward_view_uniforms";
        case ScratchTag::ForwardInstanceData:      return "forward_instance_data";
        case ScratchTag::ImmediateVertices:        return "immediate_vertices";
        case ScratchTag::Count:                    break;
    }
    return "unknown";
}

void GpuFrameScratch::BeginFrame()
{
    if (!Valid) return;
    Ring.BeginFrame();
    TagCounters.BeginFrame();
}

GpuFrameScratch::Allocation GpuFrameScratch::Allocate(VkDeviceSize size, VkDeviceSize alignment,
                                                     ScratchTag tag)
{
    if (!Valid) return {};

    const FrameScratchRing::Grant grant =
        Ring.Allocate(size, alignment == 0 ? 1 : alignment);
    if (!grant.IsValid())
    {
        TagCounters.RecordFailure(tag);
        if (size != 0)
        {
            Log.Error("GpuFrameScratch: {} asked for {} bytes at cursor {}, past the "
                      "frame slice capacity ({})",
                      ToString(tag),
                      static_cast<uint64_t>(size),
                      static_cast<uint64_t>(Ring.GetUsedBytes()),
                      static_cast<uint64_t>(Ring.GetBytesPerFrame()));
        }
        return {};
    }
    TagCounters.RecordGrant(tag, grant.Bytes);
    return MakeAllocation(grant);
}

GpuFrameScratch::Allocation GpuFrameScratch::MakeAllocation(
    const FrameScratchRing::Grant& grant) const
{
    Allocation out;
    out.Buffer = RingBuffer;
    out.Offset = grant.Offset;
    out.Mapped = static_cast<uint8_t*>(MappedBase) + grant.Offset;
    return out;
}

GpuFrameScratch::Allocation GpuFrameScratch::AllocateUniform(VkDeviceSize size, ScratchTag tag)
{
    return Allocate(size, UniformAlignment, tag);
}

GpuFrameScratch::Allocation GpuFrameScratch::AllocateVertex(VkDeviceSize size, ScratchTag tag)
{
    return Allocate(size, kVertexAlignment, tag);
}

GpuFrameScratch::ElementAllocation GpuFrameScratch::AllocateVertexElements(
    uint32_t maxElements, VkDeviceSize stride, ScratchTag tag)
{
    if (!Valid) return {};

    const FrameScratchRing::Grant grant =
        Ring.AllocateElements(maxElements, stride, kVertexAlignment);
    if (!grant.IsValid())
    {
        TagCounters.RecordFailure(tag);
        if (maxElements != 0 && stride != 0)
        {
            Log.Error("GpuFrameScratch: {} had no room for a {}-byte element at cursor "
                      "{} of frame slice capacity ({})",
                      ToString(tag),
                      static_cast<uint64_t>(stride),
                      static_cast<uint64_t>(Ring.GetUsedBytes()),
                      static_cast<uint64_t>(Ring.GetBytesPerFrame()));
        }
        return {};
    }
    TagCounters.RecordGrant(tag, grant.Bytes);

    ElementAllocation out;
    out.Grant = MakeAllocation(grant);
    out.Count = static_cast<uint32_t>(grant.Bytes / stride);
    return out;
}
