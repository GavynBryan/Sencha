#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDeviceService;
class VulkanQueueService;

//=============================================================================
// VulkanUploadContextService
//
// One-shot submit-and-wait substrate shared by every service that needs to
// copy data to the GPU outside the per-frame command loop (buffer staging,
// image staging, mip generation, etc.).
//
// Owns:
//   - a transient command pool on the graphics queue family
//   - a single upload fence
//
// Consolidating these into one service (rather than one-per-resource) gives
// us a single serialization point for asynchronous asset upload. When we
// later decouple uploads from the graphics queue, that change lives here
// and nothing downstream cares.
//
// Usage:
//     VkCommandBuffer cmd = upload.Begin();
//     // record vkCmdCopyBuffer / vkCmdBlitImage / barriers
//     if (!upload.Submit(cmd)) { /* handle */ }
//
// NOT thread-safe. A single pool + fence means the whole upload path is
// effectively serialized. If a future asset streamer needs concurrent
// uploads, this grows a per-worker pool; the public API above does not
// need to change.
//=============================================================================
class VulkanUploadContextService : public IService
{
public:
    VulkanUploadContextService(LoggingProvider& logging,
                               VulkanDeviceService& device,
                               VulkanQueueService& queues);
    ~VulkanUploadContextService() override;

    VulkanUploadContextService(const VulkanUploadContextService&) = delete;
    VulkanUploadContextService& operator=(const VulkanUploadContextService&) = delete;
    VulkanUploadContextService(VulkanUploadContextService&&) = delete;
    VulkanUploadContextService& operator=(VulkanUploadContextService&&) = delete;

    [[nodiscard]] bool IsValid() const { return UploadPool != VK_NULL_HANDLE; }

    // Allocate and begin a one-shot primary command buffer. Returns
    // VK_NULL_HANDLE on failure. The caller must follow up with Submit().
    [[nodiscard]] VkCommandBuffer Begin();

    // End, submit on the upload queue, wait on the fence, free the command
    // buffer. Returns true on success. Safe to call even if Begin() failed —
    // passing VK_NULL_HANDLE is a no-op returning false.
    bool Submit(VkCommandBuffer cmd);

private:
    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue UploadQueue = VK_NULL_HANDLE;
    uint32_t UploadQueueFamily = 0;

    VkCommandPool UploadPool = VK_NULL_HANDLE;
    VkFence UploadFence = VK_NULL_HANDLE;

    [[nodiscard]] bool CreateResources();
    void DestroyResources();
};
