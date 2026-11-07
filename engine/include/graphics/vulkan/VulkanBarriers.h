#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

//=============================================================================
// VulkanBarriers
//
// Sync2 (VK_KHR_synchronization2 / Vulkan 1.3 core) barrier helpers. These
// are free functions by design: barriers are a hot-path concern and should
// not be routed through a service object. Callers own the VkCommandBuffer
// and tell the helper exactly what they want.
//
// The helpers assume the device was created with VkPhysicalDeviceVulkan13
// Features::synchronization2 enabled. VulkanDeviceService enforces this at
// device creation time, so any code reaching these helpers can rely on it.
//=============================================================================
namespace VulkanBarriers
{
    struct ImageTransition
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageLayout OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout NewLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags2 DstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags2 SrcAccess = 0;
        VkAccessFlags2 DstAccess = 0;
        VkImageAspectFlags AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t BaseMipLevel = 0;
        uint32_t LevelCount = 1;
        uint32_t BaseArrayLayer = 0;
        uint32_t LayerCount = 1;
        uint32_t SrcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t DstQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    };

    void TransitionImage(VkCommandBuffer commandBuffer, const ImageTransition& transition);

    // Convenience: swapchain image undefined/present -> transfer dst for a clear.
    void TransitionForClear(VkCommandBuffer commandBuffer,
                            VkImage image,
                            VkImageLayout oldLayout);

    // Convenience: transfer dst -> present src after a clear or blit.
    void TransitionForPresent(VkCommandBuffer commandBuffer,
                              VkImage image,
                              VkImageLayout oldLayout);

    // Convenience: any layout -> color attachment (dynamic rendering begin).
    void TransitionForColorAttachment(VkCommandBuffer commandBuffer,
                                      VkImage image,
                                      VkImageLayout oldLayout);

    // Convenience: color attachment -> present src (dynamic rendering end).
    void TransitionFromColorAttachmentToPresent(VkCommandBuffer commandBuffer,
                                                VkImage image);

    // The one layout a depth attachment is used in: both aspects once it carries a
    // stencil, since a barrier naming stencil may not use a depth-only layout.
    [[nodiscard]] VkImageLayout DepthAttachmentLayout(bool hasStencil);

    // Orders a scope's use of a depth attachment after the last scope's writes to
    // it, from `oldLayout` into DepthAttachmentLayout. A load or store op of
    // DONT_CARE still counts as a write.
    void OrderDepthAttachment(VkCommandBuffer commandBuffer, VkImage image, bool hasStencil, VkImageLayout oldLayout);
}
