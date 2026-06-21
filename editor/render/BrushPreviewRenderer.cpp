#include "BrushPreviewRenderer.h"

#include <array>
#include <utility>
#include <vector>

BrushPreviewRenderer::BrushPreviewRenderer(PreviewBuffer& preview, EditorLinePipeline& lines)
    : Preview(preview)
    , Lines(lines)
{
}

void BrushPreviewRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    const std::optional<PreviewBox>& box = Preview.GetBox();
    if (!box)
        return;

    Transform3f transform = Transform3f::Identity();
    transform.Position = box->Center;
    const std::array<Vec3d, 8> corners = BrushGeometry::ComputeCorners(BrushState{
        .Transform = transform,
        .HalfExtents = box->HalfExtents,
    });

    constexpr std::array<std::pair<int, int>, 12> edges = {{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    }};

    std::vector<EditorLineVertex> vertices;
    vertices.reserve(edges.size() * 2);
    const Vec4 color(1.0f, 0.6f, 0.0f, 1.0f);
    for (const auto& [start, end] : edges)
    {
        vertices.push_back(EditorLineVertex{ .Position = corners[start], .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = corners[end], .Color = color });
    }

    // onTop: the dragged box is never occluded, including when it rests flush on a
    // surface in perspective.
    Lines.Submit(frame, viewport, vertices, /*onTop*/ true);
}
