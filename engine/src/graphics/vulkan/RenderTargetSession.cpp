#include <graphics/vulkan/RenderTargetSession.h>

#include <graphics/vulkan/VulkanBarriers.h>

namespace
{
void TransitionColor(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = image;
    transition.OldLayout = oldLayout;
    transition.NewLayout = newLayout;
    transition.SrcStage = srcStage;
    transition.DstStage = dstStage;
    transition.SrcAccess = srcAccess;
    transition.DstAccess = dstAccess;
    transition.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(cmd, transition);
}

// Whatever the color plane held -- UNDEFINED on first use, SHADER_READ from
// when it was last sampled -- into a color attachment.
void BeginColor(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout)
{
    TransitionColor(cmd, image, oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}
} // namespace

RenderTargetSession::RenderTargetSession(VkCommandBuffer cmd, VkImage color,
                                         VkImageLayout* layout, VkImage depth)
    : Cmd(cmd)
    , Color(color)
    , Layout(layout)
{
    if (Cmd == VK_NULL_HANDLE || Color == VK_NULL_HANDLE || Layout == nullptr)
    {
        Ended = true;
        return;
    }

    BeginColor(Cmd, Color, *Layout);

    if (depth != VK_NULL_HANDLE)
    {
        // Depth is cleared and discarded each pass, so its prior contents never
        // matter and UNDEFINED is the honest source layout.
        VulkanBarriers::ImageTransition transition{};
        transition.Image = depth;
        transition.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        transition.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        transition.DstStage = transition.SrcStage;
        transition.SrcAccess = 0;
        transition.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                             | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(Cmd, transition);
    }
}

void RenderTargetSession::End()
{
    if (Ended)
        return;
    Ended = true;

    TransitionColor(Cmd, Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // The commit the callers used to owe by hand.
    *Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
