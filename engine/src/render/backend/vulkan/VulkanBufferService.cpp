#include <render/backend/vulkan/VulkanBufferService.h>

#include <render/backend/vulkan/VulkanAllocatorService.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanUploadContextService.h>

#include <cstring>

namespace
{
    // Handle layout: high 12 bits = generation, low 20 bits = slot index.
    // Generation 0 is reserved so BufferHandle{0} is always invalid.
    constexpr uint32_t kIndexBits = 20;
    constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGeneration = (1u << (32u - kIndexBits)) - 1u;

    uint32_t DecodeIndex(uint32_t id) { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }
}

VulkanBufferService::VulkanBufferService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanAllocatorService& allocator,
    VulkanUploadContextService& upload)
    : Log(logging.GetLogger<VulkanBufferService>())
    , Device(device.GetDevice())
    , Allocator(allocator.GetAllocator())
    , UploadCtx(&upload)
{
    if (!device.IsValid() || !allocator.IsValid() || !upload.IsValid())
    {
        Log.Error("Cannot create VulkanBufferService: upstream Vulkan services not valid");
        return;
    }

    // Reserve slot 0 so the zero-initialized BufferHandle is always invalid.
    Entries.emplace_back();
    Valid = true;
}

VulkanBufferService::~VulkanBufferService()
{
    for (size_t i = 1; i < Entries.size(); ++i)
    {
        auto& entry = Entries[i];
        if (entry.Buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(Allocator, entry.Buffer, entry.Allocation);
            entry = {};
        }
    }
}

BufferHandle VulkanBufferService::MakeHandle(uint32_t index, uint32_t generation) const
{
    BufferHandle h;
    h.Id = (generation << kIndexBits) | (index & kIndexMask);
    return h;
}

VulkanBufferService::BufferEntry* VulkanBufferService::Resolve(BufferHandle handle)
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Buffer == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

const VulkanBufferService::BufferEntry* VulkanBufferService::Resolve(BufferHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    const auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Buffer == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

BufferHandle VulkanBufferService::Create(const BufferCreateInfo& info)
{
    if (info.Size == 0 || info.Usage == 0)
    {
        Log.Error("BufferCreateInfo must specify nonzero Size and Usage");
        return {};
    }

    VkBufferUsageFlags usage = info.Usage;
    if (info.Memory == BufferMemory::GpuOnly)
    {
        // GpuOnly buffers are always valid transfer targets so Upload() works.
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.Size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    switch (info.Memory)
    {
    case BufferMemory::GpuOnly:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case BufferMemory::HostVisible:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case BufferMemory::Readback:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocResult{};

    const VkResult result = vmaCreateBuffer(
        Allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &allocResult);
    if (result != VK_SUCCESS)
    {
        Log.Error("vmaCreateBuffer failed with code {}", static_cast<int>(result));
        return {};
    }

    if (info.DebugName != nullptr)
    {
        vmaSetAllocationName(Allocator, allocation, info.DebugName);
    }

    // Pick a slot (reuse freed, otherwise grow).
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
            Log.Error("VulkanBufferService slot capacity exhausted");
            vmaDestroyBuffer(Allocator, buffer, allocation);
            return {};
        }
        Entries.emplace_back();
    }

    auto& entry = Entries[index];
    entry.Buffer = buffer;
    entry.Allocation = allocation;
    entry.Size = info.Size;
    entry.MappedPtr = allocResult.pMappedData;
    entry.Memory = info.Memory;

    // Bump generation; wrap is benign but we skip 0 to keep IsValid() honest.
    entry.Generation = entry.Generation + 1;
    if (entry.Generation == 0 || entry.Generation > kMaxGeneration)
    {
        entry.Generation = 1;
    }

    return MakeHandle(index, entry.Generation);
}

void VulkanBufferService::Destroy(BufferHandle handle)
{
    auto* entry = Resolve(handle);
    if (entry == nullptr) return;

    vmaDestroyBuffer(Allocator, entry->Buffer, entry->Allocation);
    entry->Buffer = VK_NULL_HANDLE;
    entry->Allocation = VK_NULL_HANDLE;
    entry->Size = 0;
    entry->MappedPtr = nullptr;

    const uint32_t index = DecodeIndex(handle.Id);
    FreeSlots.push_back(index);
}

VkBuffer VulkanBufferService::GetBuffer(BufferHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Buffer : VK_NULL_HANDLE;
}

VkDeviceSize VulkanBufferService::GetSize(BufferHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Size : 0;
}

void* VulkanBufferService::GetMappedPointer(BufferHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->MappedPtr : nullptr;
}

bool VulkanBufferService::Upload(BufferHandle handle,
                                 const void* data,
                                 VkDeviceSize size,
                                 VkDeviceSize offset)
{
    if (data == nullptr || size == 0) return false;

    auto* entry = Resolve(handle);
    if (entry == nullptr)
    {
        Log.Error("Upload: invalid BufferHandle");
        return false;
    }

    if (offset + size > entry->Size)
    {
        Log.Error("Upload: write would overflow buffer (offset={}, size={}, capacity={})",
                  static_cast<uint64_t>(offset),
                  static_cast<uint64_t>(size),
                  static_cast<uint64_t>(entry->Size));
        return false;
    }

    switch (entry->Memory)
    {
    case BufferMemory::HostVisible:
    case BufferMemory::Readback:
        return HostVisibleUpload(*entry, data, size, offset);
    case BufferMemory::GpuOnly:
        return StagedUpload(*entry, data, size, offset);
    }

    return false;
}

bool VulkanBufferService::HostVisibleUpload(BufferEntry& entry,
                                            const void* data,
                                            VkDeviceSize size,
                                            VkDeviceSize offset)
{
    if (entry.MappedPtr == nullptr)
    {
        Log.Error("HostVisible buffer has no persistent mapping");
        return false;
    }

    auto* dst = static_cast<uint8_t*>(entry.MappedPtr) + offset;
    std::memcpy(dst, data, static_cast<size_t>(size));

    const VkResult result = vmaFlushAllocation(Allocator, entry.Allocation, offset, size);
    return result == VK_SUCCESS;
}

bool VulkanBufferService::StagedUpload(BufferEntry& entry,
                                       const void* data,
                                       VkDeviceSize size,
                                       VkDeviceSize offset)
{
    // Create a transient staging buffer: host-visible, sequential write.
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAlloc{};
    stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                       | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingResult{};

    VkResult result = vmaCreateBuffer(
        Allocator, &stagingInfo, &stagingAlloc, &staging, &stagingAllocation, &stagingResult);
    if (result != VK_SUCCESS)
    {
        Log.Error("StagedUpload: staging vmaCreateBuffer failed ({})", static_cast<int>(result));
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

    VkBufferCopy copy{};
    copy.srcOffset = 0;
    copy.dstOffset = offset;
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging, entry.Buffer, 1, &copy);

    const bool ok = UploadCtx->Submit(cmd);

    vmaDestroyBuffer(Allocator, staging, stagingAllocation);
    return ok;
}
