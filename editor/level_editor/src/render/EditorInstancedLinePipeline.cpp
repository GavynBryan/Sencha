#include "EditorInstancedLinePipeline.h"

#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineInstancedVertSpv.h>

#include <cstddef>

static_assert(sizeof(EditorLineInstance) == 64);

void EditorInstancedLinePipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorLineInstancedVertSpv;
    config.VertexWordCount = kEditorLineInstancedVertSpvWordCount;
    config.VertexName = "Editor instanced line vertex";
    config.FragmentSpirv = kEditorLineFragSpv;
    config.FragmentWordCount = kEditorLineFragSpvWordCount;
    config.FragmentName = "Editor line fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    config.DepthWrite = false;
    config.InstanceStride = sizeof(EditorLineInstance);
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

void EditorInstancedLinePipeline::Submit(const FrameContext& frame,
                                         const EditorViewport& viewport,
                                         const CameraRenderData& camera,
                                         std::span<const EditorLineVertex> vertices,
                                         std::span<const EditorLineInstance> instances,
                                         bool onTop)
{
    Pipeline.SubmitInstanced(frame, viewport, camera, vertices,
                             std::as_bytes(instances),
                             static_cast<std::uint32_t>(instances.size()), onTop);
}

void EditorInstancedLinePipeline::Teardown()
{
    Pipeline.Teardown();
}
