#include "WireframeRenderer.h"

#include "../document/SceneBrushWalk.h"

#include <cstddef>
#include <vector>

WireframeRenderer::WireframeRenderer(EditorScene& scene, EditorLinePipeline& lines)
    : Scene(scene)
    , Lines(lines)
{
}

void WireframeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    DrawWireframe(frame, viewport, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

void WireframeRenderer::DrawWireframe(const FrameContext& frame, const EditorViewport& viewport, const Vec4& color)
{
    std::vector<EditorLineVertex> vertices;
    vertices.reserve(Scene.GetEntityCount() * 24);
    ForEachVisibleBrush(Scene, /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f& transform)
        { AppendBrushMesh(vertices, mesh, transform, color); });

    Lines.Submit(frame, viewport, vertices);
}

void WireframeRenderer::AppendBrushMesh(std::vector<EditorLineVertex>& vertices,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color) const
{
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d a = transform.TransformPoint(mesh.Vertices[face.Loop[i]].Position);
            const Vec3d b = transform.TransformPoint(mesh.Vertices[face.Loop[(i + 1) % n]].Position);
            vertices.push_back(EditorLineVertex{ .Position = a, .Color = color });
            vertices.push_back(EditorLineVertex{ .Position = b, .Color = color });
        }
    }
}
