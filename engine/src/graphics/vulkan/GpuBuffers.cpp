#include <graphics/GpuBuffers.h>

#include <graphics/vulkan/VulkanBufferService.h>

namespace
{
[[nodiscard]] VkBufferUsageFlags ToVulkanUsage(GpuBufferUsage usage)
{
    VkBufferUsageFlags flags = 0;
    if (HasUsage(usage, GpuBufferUsage::Vertex))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasUsage(usage, GpuBufferUsage::Index))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasUsage(usage, GpuBufferUsage::Storage))
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return flags;
}
} // namespace

BufferHandle GpuBuffers::Create(const BufferDesc& desc)
{
    if (Impl == nullptr)
        return {};
    return Impl->Create(BufferCreateInfo{
        .Size = desc.Size,
        .Usage = ToVulkanUsage(desc.Usage),
        .Memory = desc.Memory,
        .DebugName = desc.DebugName,
    });
}

bool GpuBuffers::Upload(BufferHandle handle, const void* data, std::uint64_t size,
                        std::uint64_t offset)
{
    if (Impl == nullptr)
        return false;
    return Impl->Upload(handle, data, size, offset);
}

void GpuBuffers::Destroy(BufferHandle handle)
{
    if (Impl != nullptr)
        Impl->Destroy(handle);
}
