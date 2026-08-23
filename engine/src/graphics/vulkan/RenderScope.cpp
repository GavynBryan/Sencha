#include <graphics/vulkan/RenderScope.h>

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
