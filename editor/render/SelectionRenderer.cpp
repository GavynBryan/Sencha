#include "SelectionRenderer.h"

#include "../editmodes/ManipulatorSession.h"
#include "../meshedit/MeshElements.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

SelectionRenderer::SelectionRenderer(LevelScene& scene, SelectionService& selection,
                                     ManipulatorSession& session, EditorLinePipeline& lines)
    : Scene(scene)
    , Selection(selection)
    , Session(session)
    , Lines(lines)
{
}

void SelectionRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    std::vector<EditorLineVertex> vertices;

    const std::span<const SelectableRef> selection = Selection.GetSelection();
    vertices.reserve(selection.size() * 32);
    for (SelectableRef selected : selection)
    {
        if (!selected.IsValid() || selected.Registry != Scene.GetRegistry().Id)
            continue;

        const BrushMesh* mesh = Scene.TryGetBrushMesh(selected.Entity);
        const Transform3f* transform = Scene.TryGetTransform(selected.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        AppendBrushMesh(vertices, *mesh, *transform, Vec4(1.0f, 1.0f, 0.0f, 1.0f));

        if (selected.IsFace())
        {
            if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, selected.ElementId))
                AppendFace(vertices, *face, Vec4(1.0f, 0.4f, 0.1f, 1.0f));
        }
        else if (selected.IsEdge())
        {
            if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, selected.ElementId))
                AppendEdge(vertices, *edge, Vec4(0.2f, 0.9f, 1.0f, 1.0f));
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
                AppendVertex(vertices, *vertex, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape. (08-select-tool-v2.md)
    AppendManipulators(vertices, viewport);

    Lines.Submit(frame, viewport, vertices);
}

void SelectionRenderer::AppendBrushMesh(std::vector<EditorLineVertex>& vertices,
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

void SelectionRenderer::AppendFace(std::vector<EditorLineVertex>& vertices,
                                   const FaceElement& face,
                                   const Vec4& color) const
{
    for (size_t i = 0; i < face.Corners.size(); ++i)
    {
        const Vec3d& start = face.Corners[i];
        const Vec3d& end = face.Corners[(i + 1) % face.Corners.size()];
        vertices.push_back(EditorLineVertex{ .Position = start, .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = end, .Color = color });
    }
}

void SelectionRenderer::AppendEdge(std::vector<EditorLineVertex>& vertices,
                                   const EdgeElement& edge,
                                   const Vec4& color) const
{
    vertices.push_back(EditorLineVertex{ .Position = edge.A, .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = edge.B, .Color = color });
}

void SelectionRenderer::AppendVertex(std::vector<EditorLineVertex>& vertices,
                                     const VertexElement& vertex,
                                     const Vec4& color) const
{
    constexpr float radius = 0.05f;
    const Vec3d p = vertex.Position;
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(-radius, 0.0f, 0.0f), .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(radius, 0.0f, 0.0f), .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(0.0f, -radius, 0.0f), .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(0.0f, radius, 0.0f), .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(0.0f, 0.0f, -radius), .Color = color });
    vertices.push_back(EditorLineVertex{ .Position = p + Vec3d(0.0f, 0.0f, radius), .Color = color });
}

void SelectionRenderer::AppendManipulators(std::vector<EditorLineVertex>& vertices,
                                           const EditorViewport& viewport) const
{
    // Each active manipulator draws itself; the renderer just converts the line
    // list. Whatever the manipulators are (translate arrows now, bounds handles /
    // rotate rings / scale later), this code is unchanged.
    ManipulatorVisual visual;
    Session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
    {
        vertices.push_back(EditorLineVertex{ .Position = line.A, .Color = line.Color });
        vertices.push_back(EditorLineVertex{ .Position = line.B, .Color = line.Color });
    }
}
