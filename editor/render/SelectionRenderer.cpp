#include "SelectionRenderer.h"

#include "../EditorTheme.h"
#include "../editmodes/ManipulatorSession.h"
#include "../level/BrushGeometry.h"
#include "../meshedit/MeshElements.h"
#include "../viewport/ViewportProjection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
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
    const ViewportProjection projection(viewport); // for screen-constant vertex dots

    const std::span<const SelectableRef> selection = Selection.GetSelection();
    vertices.reserve(selection.size() * 32);
    for (SelectableRef selected : selection)
    {
        if (!selected.IsValid() || selected.Registry != Scene.GetRegistry().Id)
            continue;
        if (!Scene.IsEntityVisible(selected.Entity))
            continue;

        const BrushMesh* mesh = Scene.TryGetBrushMesh(selected.Entity);
        const Transform3f* transform = Scene.TryGetTransform(selected.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        if (selected.IsFace())
        {
            if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, selected.ElementId))
                AppendFace(vertices, *face, EditorTheme::FaceHighlight);
        }
        else if (selected.IsEdge())
        {
            if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, selected.ElementId))
                AppendEdge(vertices, *edge, EditorTheme::EdgeHighlight);
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
            {
                const float radius = projection.WorldSizeForPixels(vertex->Position, EditorTheme::VertexDotPixels) * 0.5f;
                AppendVertex(vertices, *vertex, EditorTheme::VertexHighlight, radius);
            }
        }
        else // object/entity: a clean amber bounding box, not the full wireframe
        {
            AppendAABB(vertices, *mesh, *transform, EditorTheme::Selection);
        }
    }

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape. (08-select-tool-v2.md)
    AppendManipulators(vertices, viewport);

    // Selection feedback and gizmos read better drawn on top of geometry.
    Lines.Submit(frame, viewport, vertices, /*onTop*/ true);
}

void SelectionRenderer::AppendAABB(std::vector<EditorLineVertex>& vertices,
                                   const BrushMesh& mesh,
                                   const Transform3f& transform,
                                   const Vec4& color) const
{
    const Aabb3d bounds = BrushGeometry::ComputeWorldBounds(mesh, transform);
    if (!bounds.IsValid())
        return;
    const Vec3d mn = bounds.Min;
    const Vec3d mx = bounds.Max;

    const auto corner = [&](bool xs, bool ys, bool zs) {
        return Vec3d(xs ? mx.X : mn.X, ys ? mx.Y : mn.Y, zs ? mx.Z : mn.Z);
    };
    const Vec3d c[8] = {
        corner(false, false, false), corner(true, false, false),
        corner(true, true, false),   corner(false, true, false),
        corner(false, false, true),  corner(true, false, true),
        corner(true, true, true),    corner(false, true, true),
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7},
    };
    for (const auto& e : edges)
    {
        vertices.push_back(EditorLineVertex{ .Position = c[e[0]], .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = c[e[1]], .Color = color });
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
                                     const Vec4& color,
                                     float radius) const
{
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
    // list. Whatever the manipulators are (translate arrows, bounds handles,
    // rotate/scale later), this code is unchanged.
    ManipulatorVisual visual;
    Session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
    {
        vertices.push_back(EditorLineVertex{ .Position = line.A, .Color = line.Color });
        vertices.push_back(EditorLineVertex{ .Position = line.B, .Color = line.Color });
    }
}
