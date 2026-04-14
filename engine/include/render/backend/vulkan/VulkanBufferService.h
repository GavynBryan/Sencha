#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanAllocatorService;
class VulkanDeviceService;
class VulkanQueueService;

//=============================================================================
// VulkanBufferService
//
// Owns every VkBuffer the engine allocates. Downstream services and features
// reference buffers through opaque BufferHandle values, never raw VkBuffer
// pointers — this keeps call sites backend-agnostic and lets the service
// detect use-after-free via generation counters.
//
// Three memory classes cover the allocation patterns a renderer needs:
//   - GpuOnly     : VMA AUTO_PREFER_DEVICE, no host visibility. Upload()
//                   stages through a transient host-visible buffer.
//   - HostVisible : persistently mapped, sequential-write friendly. Used
//                   for per-frame UBOs / dynamic vertex streams. Upload()
//                   becomes a memcpy + flush.
//   - Readback    : host-visible, random-access. For GPU -> CPU traffic.
//
// Upload() is synchronous by design. It submits a one-shot transfer command
// on the graphics queue and waits on a fence. Suitable for asset upload and
// startup-time population. Per-frame streaming will ride a different path
// (frame scratch allocator, step 8).
//
// Using the graphics queue for uploads is a deliberate MVP choice: it avoids
// queue-family ownership transfers entirely. Moving to the dedicated
// transfer queue becomes a later optimization with a release/acquire barrier
// pair, not an API change.
//=============================================================================
enum class BufferMemory : uint8_t
{
    GpuOnly,
    HostVisible,
    Readback,
};

struct BufferHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const BufferHandle&) const = default;
};

struct BufferCreateInfo
{
    VkDeviceSize Size = 0;
    VkBufferUsageFlags Usage = 0;
    BufferMemory Memory = BufferMemory::GpuOnly;
    const char* DebugName = nullptr;
};

class VulkanBufferService : public IService
{
public:
    VulkanBufferService(LoggingProvider& logging,
                        VulkanDeviceService& device,
                        VulkanQueueService& queues,
                        VulkanAllocatorService& allocator);
    ~VulkanBufferService() override;

    VulkanBufferService(const VulkanBufferService&) = delete;
    VulkanBufferService& operator=(const VulkanBufferService&) = delete;
    VulkanBufferService(VulkanBufferService&&) = delete;
    VulkanBufferService& operator=(VulkanBufferService&&) = delete;

    [[nodiscard]] bool IsValid() const { return UploadPool != VK_NULL_HANDLE; }

    // -- Resource lifetime --------------------------------------------------

    [[nodiscard]] BufferHandle Create(const BufferCreateInfo& info);
    void Destroy(BufferHandle handle);

    // -- Data movement ------------------------------------------------------
    //
    // Upload the given bytes into `handle` at `offset`. For GpuOnly memory
    // this stages through a transient host-visible buffer and blocks on a
    // fence. For HostVisible memory it memcpys into the persistent mapping
    // and flushes if the allocation is non-coherent.
    bool Upload(BufferHandle handle,
                const void* data,
                VkDeviceSize size,
                VkDeviceSize offset = 0);

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] VkBuffer GetBuffer(BufferHandle handle) const;
    [[nodiscard]] VkDeviceSize GetSize(BufferHandle handle) const;
    [[nodiscard]] void* GetMappedPointer(BufferHandle handle) const;

private:
    struct BufferEntry
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        void* MappedPtr = nullptr;
        BufferMemory Memory = BufferMemory::GpuOnly;
        uint32_t Generation = 0; // 0 = free slot
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue UploadQueue = VK_NULL_HANDLE;
    uint32_t UploadQueueFamily = 0;
    VmaAllocator Allocator = VK_NULL_HANDLE;

    VkCommandPool UploadPool = VK_NULL_HANDLE;
    VkFence UploadFence = VK_NULL_HANDLE;

    std::vector<BufferEntry> Entries; // slot 0 reserved
    std::vector<uint32_t> FreeSlots;

    [[nodiscard]] BufferEntry* Resolve(BufferHandle handle);
    [[nodiscard]] const BufferEntry* Resolve(BufferHandle handle) const;
    [[nodiscard]] BufferHandle MakeHandle(uint32_t index, uint32_t generation) const;

    [[nodiscard]] bool CreateUploadResources();
    void DestroyUploadResources();

    [[nodiscard]] bool StagedUpload(BufferEntry& entry,
                                    const void* data,
                                    VkDeviceSize size,
                                    VkDeviceSize offset);
    [[nodiscard]] bool HostVisibleUpload(BufferEntry& entry,
                                         const void* data,
                                         VkDeviceSize size,
                                         VkDeviceSize offset);
};
