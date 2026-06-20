#include "EditorSolidPipeline.h"

#include <shaders/kEditorSolidFragSpv.h>
#include <shaders/kEditorSolidVertSpv.h>

#include <cstddef>

void EditorSolidPipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorSolidVertSpv;
    config.VertexWordCount = kEditorSolidVertSpvWordCount;
    config.VertexName = "Editor solid vertex";
    config.FragmentSpirv = kEditorSolidFragSpv;
    config.FragmentWordCount = kEditorSolidFragSpvWordCount;
    config.FragmentName = "Editor solid fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.DepthWrite = true; // opaque: occlude correctly and let wireframe sit on top
    config.Attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EditorSolidVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EditorSolidVertex, Normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(EditorSolidVertex, Uv) },
        { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorSolidVertex, Tint) },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorSolidPipeline::Submit(const FrameContext& frame,
                                 const EditorViewport& viewport,
                                 std::span<const EditorSolidVertex> vertices)
{
    Pipeline.Submit(frame, viewport, vertices);
}

void EditorSolidPipeline::Teardown()
{
    Pipeline.Teardown();
}
