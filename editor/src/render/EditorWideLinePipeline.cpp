#include "EditorWideLinePipeline.h"

#include <shaders/kEditorWideLineFragSpv.h>
#include <shaders/kEditorWideLineVertSpv.h>

#include <cstddef>

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
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(EditorWideLineVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(EditorWideLineVertex, Other) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorWideLineVertex, Color) },
        { 3, 0, VK_FORMAT_R32_SFLOAT,          offsetof(EditorWideLineVertex, HalfWidthPx) },
        { 4, 0, VK_FORMAT_R32_SFLOAT,          offsetof(EditorWideLineVertex, Side) },
    };
    Pipeline.Setup(services, std::move(config));
}

void EditorWideLinePipeline::Submit(const FrameContext& frame,
                                    const EditorViewport& viewport,
                                    std::span<const EditorLineSegment> segments,
                                    bool onTop)
{
    if (segments.empty())
        return;

    Expanded.clear();
    Expanded.reserve(segments.size() * 6);
    for (const EditorLineSegment& s : segments)
    {
        const float half = s.WidthPx * 0.5f;
        const EditorWideLineVertex aMinus{ s.A, s.B, s.Color, half, -1.0f };
        const EditorWideLineVertex aPlus{ s.A, s.B, s.Color, half, +1.0f };
        const EditorWideLineVertex bMinus{ s.B, s.A, s.Color, half, -1.0f };
        const EditorWideLineVertex bPlus{ s.B, s.A, s.Color, half, +1.0f };
        // Two triangles tiling the ribbon quad: aMinus/aPlus at A and bMinus/bPlus at B,
        // on the -/+ perpendicular sides. The vertex shader canonicalizes the
        // perpendicular so both endpoints agree on which side is which; aPlus and bMinus
        // are then opposite corners, so these triangles share that diagonal and tile
        // cleanly (and the AA coordinate runs consistently across the quad).
        Expanded.push_back(aMinus);
        Expanded.push_back(aPlus);
        Expanded.push_back(bMinus);
        Expanded.push_back(bMinus);
        Expanded.push_back(aPlus);
        Expanded.push_back(bPlus);
    }

    Pipeline.Submit(frame, viewport, std::span<const EditorWideLineVertex>(Expanded), onTop);
}

void EditorWideLinePipeline::Teardown()
{
    Pipeline.Teardown();
    Expanded.clear();
    Expanded.shrink_to_fit();
}
