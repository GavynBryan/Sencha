#include "render/SceneThenDisplay.h"

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanFormat.h>

void OrderViewDepthAccess(VkCommandBuffer cmd, VkImage depth, VkFormat format)
{
    const bool hasStencil = FormatHasStencil(format);
    VulkanBarriers::OrderDepthAttachment(cmd, depth, hasStencil, VulkanBarriers::DepthAttachmentLayout(hasStencil));
}

void RecordSceneThenDisplay(const FrameContext& frame, const SceneDisplayTargets& targets, VkClearColorValue clear,
                            OutputTransformPass& output, const OutputTransformParams& params,
                            const std::function<void(const FrameContext&)>& drawScene,
                            const std::function<void(const FrameContext&)>& drawBetween,
                            const std::function<void(const FrameContext&)>& drawDisplay, ScenePostState* post)
{
    RenderTargetSession displaySession(frame.Cmd, targets.DisplayImage, targets.DisplayLayout, targets.DepthImage,
                                       targets.DepthFormat);
    if (!drawScene)
    {
        // Nothing emits light, so there is nothing to transform: the display clears.
        RenderScopeDesc desc{};
        desc.Area.extent = targets.Extent;
        desc.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.DepthFormat = targets.DepthFormat;
        desc.Phase = RenderPhase::Offscreen;
        desc.Color.View = targets.DisplayView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Color.Clear.color = clear;
        desc.Depth.View = targets.DepthView;
        desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.Depth.Clear.depthStencil = { 1.0f, 0 };
        {
            const RenderScope display(frame, desc);
            drawDisplay(display.Context());
        }
        displaySession.End();
        return;
    }

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

    OrderViewDepthAccess(frame.Cmd, targets.DepthImage, targets.DepthFormat);
    if (drawBetween)
    {
        FrameContext between = frame;
        between.SceneColorBindless = targets.SceneBindless;
        between.SceneExtent = targets.Extent;
        between.ScenePost = post;
        between.DepthView = targets.DepthView;
        between.DepthFormat = targets.DepthFormat;
        between.StencilFormat = FormatHasStencil(targets.DepthFormat) ? targets.DepthFormat : VK_FORMAT_UNDEFINED;
        drawBetween(between);
        OrderViewDepthAccess(frame.Cmd, targets.DepthImage, targets.DepthFormat);
    }

    {
        desc.Color.View = targets.DisplayView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        const RenderScope display(frame, desc);
        FrameContext context = display.Context();
        context.SceneColorBindless = targets.SceneBindless;
        context.SceneExtent = targets.Extent;
        context.ScenePost = post;
        output.Draw(context, params);
        drawDisplay(context);
    }
    displaySession.End();
}
