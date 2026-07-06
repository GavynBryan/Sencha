#include "BrushPreviewRenderer.h"

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

BrushPreviewRenderer::BrushPreviewRenderer(PreviewBuffer& preview, EditorLinePipeline& lines)
    : Preview(preview)
    , Lines(lines)
{
}

void BrushPreviewRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    const std::optional<PreviewMesh>& preview = Preview.GetMesh();
    if (!preview)
        return;

    const BrushMesh& mesh = preview->Mesh;
    const Transform3f& transform = preview->Transform;

    // One line per unique undirected edge across all face loops; works for any
    // primitive (box, plane quad, cylinder), no per-shape geometry.
    std::unordered_set<std::uint64_t> seen;
    std::vector<EditorLineVertex> vertices;
    const Vec4 color(1.0f, 0.6f, 0.0f, 1.0f);
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::uint32_t a = face.Loop[i];
            const std::uint32_t b = face.Loop[(i + 1) % n];
            const std::uint32_t lo = a < b ? a : b;
            const std::uint32_t hi = a < b ? b : a;
            const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | hi;
            if (!seen.insert(key).second)
                continue;
            vertices.push_back(EditorLineVertex{
                .Position = transform.TransformPoint(mesh.Vertices[a].Position), .Color = color });
            vertices.push_back(EditorLineVertex{
                .Position = transform.TransformPoint(mesh.Vertices[b].Position), .Color = color });
        }
    }

    // onTop: the dragged preview is never occluded, including when it rests flush
    // on a surface in perspective.
    Lines.Submit(frame, viewport, vertices, /*onTop*/ true);
}
