#include "EditorFillPipeline.h"

#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>

#include <cstddef>

void EditorFillPipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorLineVertSpv;
    config.VertexWordCount = kEditorLineVertSpvWordCount;
    config.VertexName = "Editor fill vertex";
    config.FragmentSpirv = kEditorLineFragSpv;
    config.FragmentWordCount = kEditorLineFragSpvWordCount;
    config.FragmentName = "Editor fill fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.CullMode = VK_CULL_MODE_NONE; // a selected face reads from either side
    config.DepthWrite = false;
    config.Blend.BlendEnable = true;
    config.Blend.SrcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.Blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.ColorOp = VK_BLEND_OP_ADD;
    config.Blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    config.Blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.AlphaOp = VK_BLEND_OP_ADD;
    config.Attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EditorLineVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorLineVertex, Color) },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorFillPipeline::Submit(const FrameContext& frame,
                                const EditorViewport& viewport,
                                std::span<const EditorLineVertex> vertices,
                                bool onTop)
{
    Pipeline.Submit(frame, viewport, vertices, onTop);
}

void EditorFillPipeline::Teardown()
{
    Pipeline.Teardown();
}
