#include "EditorLinePipeline.h"

#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>

#include <cstddef>

void EditorLinePipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorLineVertSpv;
    config.VertexWordCount = kEditorLineVertSpvWordCount;
    config.VertexName = "Editor line vertex";
    config.FragmentSpirv = kEditorLineFragSpv;
    config.FragmentWordCount = kEditorLineFragSpvWordCount;
    config.FragmentName = "Editor line fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    config.DepthWrite = false;
    config.Attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EditorLineVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorLineVertex, Color) },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorLinePipeline::Submit(const FrameContext& frame,
                                const EditorViewport& viewport,
                                std::span<const EditorLineVertex> vertices,
                                bool onTop)
{
    Pipeline.Submit(frame, viewport, vertices, onTop);
}

void EditorLinePipeline::Teardown()
{
    Pipeline.Teardown();
}
