#include <graphics/vulkan/RenderScope.h>

#include <cassert>

namespace
{
VkRenderingAttachmentInfo MakeAttachment(const RenderScopeAttachment& attachment,
                                         VkImageLayout layout)
{
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = attachment.View;
    info.imageLayout = layout;
    info.loadOp = attachment.LoadOp;
    info.storeOp = attachment.StoreOp;
    info.clearValue = attachment.Clear;
    return info;
}
} // namespace

FrameContext MakeScopeContext(const FrameContext& outer, const RenderScopeDesc& desc)
{
    // Copied from the outer context rather than default-constructed, so a field
    // added to FrameContext reaches nested passes without an edit here.
    FrameContext inner = outer;
    inner.TargetExtent = desc.Area.extent;
    inner.TargetFormat = desc.ColorFormat;
    inner.TargetView = desc.Color.View;
    inner.DepthView = desc.Depth.View;
    inner.DepthFormat = desc.DepthFormat;
    inner.Phase = desc.Phase;
    return inner;
}

RenderScope::RenderScope(const FrameContext& frame, const RenderScopeDesc& desc)
    : Cmd(frame.Cmd)
{
    const VkRenderingAttachmentInfo color =
        MakeAttachment(desc.Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkRenderingAttachmentInfo depth =
        MakeAttachment(desc.Depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = desc.Area;
    info.layerCount = 1;
    if (desc.Color.IsPresent())
    {
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
    }
    if (desc.Depth.IsPresent())
        info.pDepthAttachment = &depth;

    vkCmdBeginRendering(Cmd, &info);

    Inner = MakeScopeContext(frame, desc);
}

RenderScope::~RenderScope()
{
    vkCmdEndRendering(Cmd);
}

RenderScopeInterruption::RenderScopeInterruption(const FrameContext& frame)
    : Frame(frame)
{
    vkCmdEndRendering(Frame.Cmd);

    // Dependency 1: the suspended instance's depth writes must be visible to
    // the intervening pass's read-only depth test. Layout is unchanged.
    if (Frame.DepthView != VK_NULL_HANDLE)
    {
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        VkDependencyInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.memoryBarrierCount = 1;
        info.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(Frame.Cmd, &info);
    }
}

void RenderScopeInterruption::Resume()
{
    if (Resumed)
        return;
    Resumed = true;

    // Dependencies 3 and 4: the suspended instance's colour writes, and the
    // intervening depth reads, both complete before the resumed instance
    // LOADs and touches the same attachments.
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                         | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                          | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                         | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                          | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                          | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                          | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkDependencyInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    info.memoryBarrierCount = 1;
    info.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(Frame.Cmd, &info);

    RenderScopeDesc desc{};
    desc.Area.extent = Frame.TargetExtent;
    desc.Color.View = Frame.TargetView;
    desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    desc.ColorFormat = Frame.TargetFormat;
    desc.Depth.View = Frame.DepthView;
    desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    // The resumed instance is the phase's last user of depth this frame;
    // contents are cleared next frame, so nothing needs them stored again.
    desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    desc.DepthFormat = Frame.DepthFormat;
    desc.Phase = Frame.Phase;

    // Re-begin without RenderScope's RAII: the outer owner's vkCmdEndRendering
    // closes this instance, exactly as it would have closed the original.
    const VkRenderingAttachmentInfo color =
        MakeAttachment(desc.Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkRenderingAttachmentInfo depth =
        MakeAttachment(desc.Depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = desc.Area;
    rendering.layerCount = 1;
    if (desc.Color.IsPresent())
    {
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
    }
    rendering.pDepthAttachment = desc.Depth.IsPresent() ? &depth : nullptr;
    vkCmdBeginRendering(Frame.Cmd, &rendering);
}

RenderScopeInterruption::~RenderScopeInterruption()
{
    // The phase must not be left suspended: resume on the caller's behalf and
    // flag the omission where it can be seen.
    assert(Resumed && "RenderScopeInterruption destroyed without Resume()");
    Resume();
}
