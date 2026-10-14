#include "EditorFillPipeline.h"

#include <utility>

void EditorFillPipeline::Setup(const RendererServices& services)
{
    ImmediatePipelineConfig config = ColoredVertexConfig(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    config.CullMode = VK_CULL_MODE_NONE; // a selected face reads from either side
    config.Blend.BlendEnable = true;
    config.Blend.SrcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.Blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.ColorOp = VK_BLEND_OP_ADD;
    config.Blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    config.Blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.AlphaOp = VK_BLEND_OP_ADD;
    Pipeline.Setup(services, std::move(config));
}

void EditorFillPipeline::Submit(const FrameContext& frame,
                                const EditorViewport& viewport,
                                const CameraRenderData& camera,
                                std::span<const ColoredVertex> vertices,
                                bool onTop)
{
    Pipeline.Submit(frame, viewport.Region(), camera, vertices, onTop);
}

void EditorFillPipeline::Teardown()
{
    Pipeline.Teardown();
}
