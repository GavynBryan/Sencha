#include <graphics/vulkan/VulkanBarriers.h>

namespace VulkanBarriers
{
    void TransitionImage(VkCommandBuffer commandBuffer, const ImageTransition& transition)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = transition.SrcStage;
        barrier.srcAccessMask = transition.SrcAccess;
        barrier.dstStageMask = transition.DstStage;
        barrier.dstAccessMask = transition.DstAccess;
        barrier.oldLayout = transition.OldLayout;
        barrier.newLayout = transition.NewLayout;
        barrier.srcQueueFamilyIndex = transition.SrcQueueFamily;
        barrier.dstQueueFamilyIndex = transition.DstQueueFamily;
        barrier.image = transition.Image;
        barrier.subresourceRange.aspectMask = transition.AspectMask;
        barrier.subresourceRange.baseMipLevel = transition.BaseMipLevel;
        barrier.subresourceRange.levelCount = transition.LevelCount;
        barrier.subresourceRange.baseArrayLayer = transition.BaseArrayLayer;
        barrier.subresourceRange.layerCount = transition.LayerCount;

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    }

    void TransitionForClear(VkCommandBuffer commandBuffer,
                            VkImage image,
                            VkImageLayout oldLayout)
    {
        ImageTransition t{};
        t.Image = image;
        t.OldLayout = oldLayout;
        t.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        TransitionImage(commandBuffer, t);
    }

    void TransitionForPresent(VkCommandBuffer commandBuffer,
                              VkImage image,
                              VkImageLayout oldLayout)
    {
        ImageTransition t{};
        t.Image = image;
        t.OldLayout = oldLayout;
        t.NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        t.SrcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        t.SrcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        t.DstAccess = 0;
        TransitionImage(commandBuffer, t);
    }

    void TransitionForColorAttachment(VkCommandBuffer commandBuffer,
                                      VkImage image,
                                      VkImageLayout oldLayout)
    {
        ImageTransition t{};
        t.Image = image;
        t.OldLayout = oldLayout;
        t.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        TransitionImage(commandBuffer, t);
    }

    void TransitionFromColorAttachmentToPresent(VkCommandBuffer commandBuffer, VkImage image)
    {
        ImageTransition t{};
        t.Image = image;
        t.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        t.NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        t.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        t.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        t.DstAccess = 0;
        TransitionImage(commandBuffer, t);
    }
}
