#include <graphics/vulkan/DebugLinePass.h>

#include <math/geometry/2d/Rect2d.h>

void DebugLinePass::Setup(const RendererServices& services)
{
    // Lines draw in the output phase, straight to the display, so they owe the encode
    // a display with no hardware sRGB write needs.
    ImmediatePipelineConfig config = ColoredVertexConfig(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    config.EncodeForTarget = true;
    Lines.Setup(services, config);
}

void DebugLinePass::Draw(const FrameContext& frame, const CameraRenderData& camera, const DebugLineFrame& lines)
{
    const Rect2d target(0.0f, 0.0f, static_cast<float>(frame.TargetExtent.width),
                        static_cast<float>(frame.TargetExtent.height));
    Lines.Submit(frame, target, camera, lines.Tested, false);
    Lines.Submit(frame, target, camera, lines.OnTop, true);
}

void DebugLinePass::Teardown()
{
    Lines.Teardown();
}
