#include "EditorWideLinePipeline.h"

#include <shaders/kEditorWideLineFragSpv.h>
#include <shaders/kEditorWideLineVertSpv.h>

#include <cstddef>

// The segment list is uploaded as the instance stream verbatim; the attribute
// offsets below index straight into EditorLineSegment.
static_assert(offsetof(EditorLineSegment, A) == 0);
static_assert(offsetof(EditorLineSegment, B) == 12);
static_assert(offsetof(EditorLineSegment, Color) == 24);
static_assert(offsetof(EditorLineSegment, WidthPx) == 40);
static_assert(sizeof(EditorLineSegment) == 44);

void EditorWideLinePipeline::Setup(const RendererServices& services)
{
    EditorImmediatePipelineConfig config;
    config.VertexSpirv = kEditorWideLineVertSpv;
    config.VertexWordCount = kEditorWideLineVertSpvWordCount;
    config.VertexName = "Editor wide-line vertex";
    config.FragmentSpirv = kEditorWideLineFragSpv;
    config.FragmentWordCount = kEditorWideLineFragSpvWordCount;
    config.FragmentName = "Editor wide-line fragment";
    config.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.CullMode = VK_CULL_MODE_NONE;
    config.DepthWrite = false;
    config.InstanceExpansion = 6; // two triangles per segment, built in the vertex shader

    // Pull the depth-tested wireframe slightly toward the camera so edges coincident
    // with the solid surface win LESS_OR_EQUAL instead of z-fighting. Constant term
    // covers face-on edges (zero depth slope); slope term covers grazing angles.
    config.DepthBiasConstant = -1.0f;
    config.DepthBiasSlope = -1.0f;

    // Alpha blend so the analytic AA feather composites (matches GpuGridRenderer).
    config.Blend.BlendEnable = true;
    config.Blend.SrcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.Blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.ColorOp = VK_BLEND_OP_ADD;
    config.Blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    config.Blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.Blend.AlphaOp = VK_BLEND_OP_ADD;

    config.Attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(EditorLineSegment, A) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(EditorLineSegment, B) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorLineSegment, Color) },
        { 3, 0, VK_FORMAT_R32_SFLOAT,          offsetof(EditorLineSegment, WidthPx) },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorWideLinePipeline::Submit(const FrameContext& frame,
                                    const EditorViewport& viewport,
                                    const CameraRenderData& camera,
                                    std::span<const EditorLineSegment> segments,
                                    bool onTop,
                                    std::string_view source)
{
    (void)source;
    if (segments.empty())
        return;

    // Chunk to the scratch slice so a huge selection degrades to more draws
    // instead of a dropped overlay. If the frame's scratch truly runs out the
    // shared pipeline logs the drop with its own counts.
    const std::size_t maxPerSubmit = Pipeline.MaxScratchVerticesPerSubmit();
    if (maxPerSubmit == 0)
        return;
    for (std::size_t offset = 0; offset < segments.size(); offset += maxPerSubmit)
    {
        const std::size_t count = std::min(maxPerSubmit, segments.size() - offset);
        Pipeline.Submit(frame, viewport, camera, segments.subspan(offset, count), onTop);
    }
}

void EditorWideLinePipeline::Teardown()
{
    Pipeline.Teardown();
}
