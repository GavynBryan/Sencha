#include "EditorInstancedLinePipeline.h"

void EditorInstancedLinePipeline::Setup(const RendererServices& services)
{
    Pipeline.Setup(services, InstancedColoredVertexConfig(VK_PRIMITIVE_TOPOLOGY_LINE_LIST));
}

void EditorInstancedLinePipeline::Submit(const FrameContext& frame,
                                         const EditorViewport& viewport,
                                         const CameraRenderData& camera,
                                         std::span<const ColoredVertex> vertices,
                                         std::span<const ColoredVertexInstance> instances,
                                         bool onTop)
{
    Pipeline.SubmitInstanced(frame, viewport.Region(), camera, vertices,
                             std::as_bytes(instances),
                             static_cast<std::uint32_t>(instances.size()), onTop);
}

void EditorInstancedLinePipeline::Teardown()
{
    Pipeline.Teardown();
}
