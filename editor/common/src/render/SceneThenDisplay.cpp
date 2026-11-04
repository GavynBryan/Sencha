#include "render/SceneThenDisplay.h"

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanFormat.h>

namespace
{
// The display scope reads what the scene scope wrote to the same image in the
// same layout, so all this orders is the access.
void OrderDepthAfterScene(VkCommandBuffer cmd, VkImage depth, VkFormat format)
{
    const bool hasStencil = FormatHasStencil(format);
    const VkImageLayout layout = hasStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                            : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    VulkanBarriers::ImageTransition barrier{};
    barrier.Image = depth;
    barrier.OldLayout = layout;
    barrier.NewLayout = layout;
    barrier.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.DstStage = barrier.SrcStage;
    barrier.SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.AspectMask = hasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                                    : VK_IMAGE_ASPECT_DEPTH_BIT;
    VulkanBarriers::TransitionImage(cmd, barrier);
}
} // namespace

void RecordSceneThenDisplay(const FrameContext& frame, const SceneDisplayTargets& targets, VkClearColorValue clear,
                            OutputTransformPass& output, const OutputTransformParams& params,
                            const std::function<void(const FrameContext&)>& drawScene,
                            const std::function<void(const FrameContext&)>& drawDisplay)
{
    RenderTargetSession displaySession(frame.Cmd, targets.DisplayImage, targets.DisplayLayout, targets.DepthImage,
                                       targets.DepthFormat);

    RenderScopeDesc desc{};
    desc.Area.extent = targets.Extent;
    desc.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    desc.DepthFormat = targets.DepthFormat;
    desc.Phase = RenderPhase::Offscreen;
    desc.Depth.View = targets.DepthView;

    {
        RenderTargetSession sceneSession(frame.Cmd, targets.SceneImage, *targets.SceneAccess);
        desc.Color.View = targets.SceneView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Color.Clear.color = clear;
        desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.Depth.Clear.depthStencil = { 1.0f, 0 };
        const RenderScope scene(frame, desc);
        drawScene(scene.Context());
    }

    OrderDepthAfterScene(frame.Cmd, targets.DepthImage, targets.DepthFormat);

    {
        desc.Color.View = targets.DisplayView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        const RenderScope display(frame, desc);
        FrameContext context = display.Context();
        context.SceneColorBindless = targets.SceneBindless;
        context.SceneExtent = targets.Extent;
        output.Draw(context, params);
        drawDisplay(context);
    }
    displaySession.End();
}
