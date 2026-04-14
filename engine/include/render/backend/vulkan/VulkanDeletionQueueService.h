#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

//=============================================================================
// VulkanDeletionQueueService
//
// Deferred-destroy ring shared by all GPU resource services (image, buffer,
// etc.). Any Vulkan object freed at runtime must route through this service
// rather than calling vkDestroyX / vmaDestroyX directly. The queue retains
// each pending destroy for `framesInFlight + 1` frames before executing it,
// guaranteeing that no in-flight command buffer can still reference the
// resource when the destroy fires.
//
// VulkanFrameService::BeginFrame calls AdvanceFrame() immediately after
// waiting on the in-flight fence for the current slot. That wait proves that
// all GPU work submitted framesInFlight frames ago has retired, so it is safe
// to release any resource enqueued that many frames prior.
//
// At shutdown the destructor flushes every pending destroy synchronously.
// By construction, VulkanFrameService::DestroyFrameData calls vkDeviceWaitIdle
// before this service is destroyed, so residual entries are always safe to run.
//
// Thread-safety: none. All calls must originate from the render/main thread.
//
// Adding a new resource type:
//   1. Define a DeferredXxxDestroy struct below.
//   2. Add std::vector<DeferredXxxDestroy> to Bucket.
//   3. Add EnqueueXxxDestroy() public method.
//   4. Add the flush loop to AdvanceFrame() and ~VulkanDeletionQueueService().
//=============================================================================

// -- Deferred destroy entries ------------------------------------------------
//
// Plain structs — no virtual dispatch, no heap allocation, debugger-friendly.
// Each field is the minimum set of handles required to call the Vulkan / VMA
// destroy function for that resource type.

struct DeferredImageDestroy
{
    VkDevice      Device     = VK_NULL_HANDLE;
    VmaAllocator  Allocator  = VK_NULL_HANDLE;
    VkImageView   View       = VK_NULL_HANDLE;
    VkImage       Image      = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;
};

// -- VulkanDeletionQueueService ----------------------------------------------

class VulkanDeletionQueueService : public IService
{
public:
    // `framesInFlight` must equal the frames-in-flight count passed to
    // VulkanFrameService so that the ring and the fence cadence stay in sync.
    VulkanDeletionQueueService(LoggingProvider& logging, uint32_t framesInFlight);
    ~VulkanDeletionQueueService() override;

    VulkanDeletionQueueService(const VulkanDeletionQueueService&) = delete;
    VulkanDeletionQueueService& operator=(const VulkanDeletionQueueService&) = delete;
    VulkanDeletionQueueService(VulkanDeletionQueueService&&) = delete;
    VulkanDeletionQueueService& operator=(VulkanDeletionQueueService&&) = delete;

    // Enqueue a deferred image+view destroy to run after framesInFlight frames.
    void EnqueueImageDestroy(DeferredImageDestroy entry);

    // Advance the ring and flush the bucket that is now safe to release.
    // Called by VulkanFrameService::BeginFrame after the in-flight fence wait.
    void AdvanceFrame();

private:
    struct Bucket
    {
        std::vector<DeferredImageDestroy> Images;
        // Add DeferredBufferDestroy, DeferredPipelineDestroy, etc. here as needed.
    };

    void FlushBucket(Bucket& bucket);

    Logger& Log;
    std::vector<Bucket> Buckets;
    uint32_t CurrentBucket = 0;
};
