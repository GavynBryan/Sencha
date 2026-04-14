#include <render/backend/vulkan/VulkanDeletionQueueService.h>

#include <algorithm>

VulkanDeletionQueueService::VulkanDeletionQueueService(
    LoggingProvider& logging,
    uint32_t framesInFlight)
    : Log(logging.GetLogger<VulkanDeletionQueueService>())
{
    // One extra bucket so the ring never wraps back onto a bucket that may
    // still be occupied by an in-flight frame.
    const uint32_t bucketCount = std::max(framesInFlight, 1u) + 1u;
    Buckets.resize(bucketCount);
    Log.Info("VulkanDeletionQueueService created: {} buckets (framesInFlight={})",
             bucketCount, framesInFlight);
}

VulkanDeletionQueueService::~VulkanDeletionQueueService()
{
    // Flush all residual entries. By the time this destructor runs,
    // VulkanFrameService::DestroyFrameData will have called vkDeviceWaitIdle,
    // so it is safe to release any remaining GPU resources synchronously.
    for (auto& bucket : Buckets)
        FlushBucket(bucket);
}

void VulkanDeletionQueueService::EnqueueImageDestroy(DeferredImageDestroy entry)
{
    Buckets[CurrentBucket].Images.push_back(entry);
}

void VulkanDeletionQueueService::AdvanceFrame()
{
    CurrentBucket = (CurrentBucket + 1u) % static_cast<uint32_t>(Buckets.size());
    FlushBucket(Buckets[CurrentBucket]);
}

void VulkanDeletionQueueService::FlushBucket(Bucket& bucket)
{
    for (const auto& e : bucket.Images)
    {
        if (e.View  != VK_NULL_HANDLE) vkDestroyImageView(e.Device, e.View, nullptr);
        if (e.Image != VK_NULL_HANDLE) vmaDestroyImage(e.Allocator, e.Image, e.Allocation);
    }
    bucket.Images.clear();
}
