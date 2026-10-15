#include <graphics/vulkan/DebugLinePass.h>

#include <math/geometry/2d/Rect2d.h>

void DebugLinePass::Setup(const RendererServices& services)
{
    Lines.Setup(services, ColoredVertexConfig(VK_PRIMITIVE_TOPOLOGY_LINE_LIST));
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
