#include "WireframeRenderer.h"

#include "../level/BrushGeometry.h"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

WireframeRenderer::WireframeRenderer(LevelScene& scene, EditorLinePipeline& lines)
    : Scene(scene)
    , Lines(lines)
{
}

void WireframeRenderer::SetPreviewBuffer(PreviewBuffer* preview)
{
    Preview = preview;
}

void WireframeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    std::vector<EditorLineVertex> vertices;
    vertices.reserve(Scene.GetEntityCount() * 24);
    for (EntityId entity : Scene.GetAllEntities())
    {
        const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        AppendBrushMesh(vertices, *mesh, *transform, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }

    if (Preview != nullptr)
    {
        if (const auto& box = Preview->GetBox())
        {
            Transform3f previewTransform = Transform3f::Identity();
            previewTransform.Position = box->Center;
            AppendBrush(vertices, BrushState{
                .Transform = previewTransform,
                .HalfExtents = box->HalfExtents,
            }, Vec4(1.0f, 0.6f, 0.0f, 1.0f));
        }
    }

    Lines.Submit(frame, viewport, vertices);
}

void WireframeRenderer::AppendBrush(std::vector<EditorLineVertex>& vertices,
                                    const BrushState& brush,
                                    const Vec4& color) const
{
    const std::array<Vec3d, 8> corners = BrushGeometry::ComputeCorners(brush);

    constexpr std::array<std::pair<int, int>, 12> edges = {{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    }};

    for (const auto& [start, end] : edges)
    {
        vertices.push_back(EditorLineVertex{ .Position = corners[start], .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = corners[end], .Color = color });
    }
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
