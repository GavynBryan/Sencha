#include "EditorInstancedFillPipeline.h"

#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineInstancedVertSpv.h>

#include <cstddef>

void EditorInstancedFillPipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorLineInstancedVertSpv;
    config.VertexWordCount = kEditorLineInstancedVertSpvWordCount;
    config.VertexName = "Editor instanced fill vertex";
    config.FragmentSpirv = kEditorLineFragSpv;
    config.FragmentWordCount = kEditorLineFragSpvWordCount;
    config.FragmentName = "Editor fill fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.CullMode = VK_CULL_MODE_NONE; // a selected face reads from either side
    config.DepthWrite = false;
    config.InstanceStride = sizeof(EditorLineInstance);
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
        { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorInstancedFillPipeline::Submit(const FrameContext& frame,
                                         const EditorViewport& viewport,
                                         const CameraRenderData& camera,
                                         std::span<const EditorLineVertex> vertices,
                                         std::span<const EditorLineInstance> instances,
                                         bool onTop)
{
    Pipeline.SubmitInstanced(frame, viewport, camera, vertices, std::as_bytes(instances),
                             static_cast<std::uint32_t>(instances.size()), onTop);
}

void EditorInstancedFillPipeline::Teardown()
{
    Pipeline.Teardown();
}
