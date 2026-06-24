#include "SelectionRenderer.h"

#include "../EditorTheme.h"
#include "../editmodes/ManipulatorSession.h"
#include "../meshedit/MeshEditService.h"
#include "../overlay/EditorOverlayState.h"
#include "../viewport/ViewportProjection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
// View-plane right/up so a vertex square faces the camera in any view.
void ViewBasis(const EditorViewport& viewport, Vec3d& right, Vec3d& up)
{
    if (viewport.Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        right = viewport.Camera.GetOrthoRightVector();
        up = viewport.Camera.GetOrthoUpVector();
    }
    else
    {
        right = viewport.Camera.GetRightVector();
        up = viewport.Camera.GetUpVector();
    }
}
}

SelectionRenderer::SelectionRenderer(EditorScene& scene, SelectionService& selection, MeshEditService& meshEdit,
                                     const EditorOverlayState& overlay, ManipulatorSession& session,
                                     EditorLinePipeline& lines)
    : Scene(scene)
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Overlay(overlay)
    , Session(session)
    , Lines(lines)
{
}

void SelectionRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    const std::vector<EntityId> bodies = GatherActiveBodies();
    const bool vertexMode = MeshEdit.GetElementKind() == MeshElementKind::Vertex;

    // The active-body wireframe and vertex handles are occluded by solid geometry so
    // back edges/handles you can't pick aren't drawn (matching PickEdge/PickVertex,
    // which occlude only in solid shading). In wireframe/ortho they ride the on-top
    // list, since picking doesn't occlude there either.
    const bool occludeBody = viewport.Shading == ViewportShading::Solid;
    std::vector<EditorLineVertex> occluded;
    std::vector<EditorLineVertex> onTop;
    onTop.reserve(selection.size() * 32 + 64);
    std::vector<EditorLineVertex>& bodyLines = occludeBody ? occluded : onTop;

    for (EntityId entity : bodies)
    {
        const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        AppendWireframe(bodyLines, *mesh, *transform, EditorTheme::SelectedWireframe);
        if (vertexMode)
            for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
                AppendVertexSquare(bodyLines, viewport, vertex.Position, EditorTheme::VertexHandle);
    }

    // Per-element highlights, the hover glow, and the gizmos stay on top so the
    // selection and manipulators read through geometry.
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
                AppendFace(onTop, *face, EditorTheme::FaceHighlight);
        }
        else if (selected.IsEdge())
        {
            if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, selected.ElementId))
                AppendEdge(onTop, *edge, EditorTheme::EdgeHighlight);
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
                AppendVertexSquare(onTop, viewport, vertex->Position, EditorTheme::VertexHighlight);
        }
        // object: the active-body wireframe above already covers it.
    }

    AppendHover(onTop, viewport);

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape.
    AppendManipulators(onTop, viewport);

    // Body wireframe/handles depth-test against the scene (back ones cull); selection
    // feedback and gizmos draw on top.
    if (!occluded.empty())
        Lines.Submit(frame, viewport, occluded, /*onTop*/ false);
    Lines.Submit(frame, viewport, onTop, /*onTop*/ true);
}

std::vector<EntityId> SelectionRenderer::GatherActiveBodies() const
{
    std::vector<EntityId> bodies;
    for (SelectableRef ref : Selection.GetSelection())
    {
        if (!ref.IsValid() || ref.Registry != Scene.GetRegistry().Id || !ref.Entity.IsValid())
            continue;
        if (!Scene.IsEntityVisible(ref.Entity))
            continue;
        if (std::find(bodies.begin(), bodies.end(), ref.Entity) == bodies.end())
            bodies.push_back(ref.Entity);
    }
    return bodies;
}

void SelectionRenderer::AppendWireframe(std::vector<EditorLineVertex>& vertices,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color) const
{
    for (const EdgeElement& edge : MeshElements::Edges(mesh, transform))
    {
        vertices.push_back(EditorLineVertex{ .Position = edge.A, .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = edge.B, .Color = color });
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

void SelectionRenderer::AppendVertexSquare(std::vector<EditorLineVertex>& vertices,
                                           const EditorViewport& viewport,
                                           Vec3d position,
                                           const Vec4& color) const
{
    const float half = ViewportProjection(viewport).WorldSizeForPixels(position, EditorTheme::VertexDotPixels) * 0.5f;
    Vec3d right;
    Vec3d up;
    ViewBasis(viewport, right, up);

    const std::array<Vec3d, 4> corners = {
        position + right * half + up * half,
        position - right * half + up * half,
        position - right * half - up * half,
        position + right * half - up * half,
    };
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        vertices.push_back(EditorLineVertex{ .Position = corners[i], .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = corners[(i + 1) % corners.size()], .Color = color });
    }
}

void SelectionRenderer::AppendHover(std::vector<EditorLineVertex>& vertices, const EditorViewport& viewport) const
{
    const SelectableRef hovered = Overlay.Hover.Element;
    if (!hovered.IsValid() || hovered.Registry != Scene.GetRegistry().Id)
        return;
    if (!Scene.IsEntityVisible(hovered.Entity))
        return;

    const BrushMesh* mesh = Scene.TryGetBrushMesh(hovered.Entity);
    const Transform3f* transform = Scene.TryGetTransform(hovered.Entity);
    if (mesh == nullptr || transform == nullptr)
        return;

    const Vec4 color = EditorTheme::HoverEligible;
    if (hovered.IsFace())
    {
        if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, hovered.ElementId))
            AppendFace(vertices, *face, color);
    }
    else if (hovered.IsEdge())
    {
        if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, hovered.ElementId))
            AppendEdge(vertices, *edge, color);
    }
    else if (hovered.IsVertex())
    {
        if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, hovered.ElementId))
            AppendVertexSquare(vertices, viewport, vertex->Position, color);
    }
    else // object: glow its wireframe so you see what a click would select
    {
        AppendWireframe(vertices, *mesh, *transform, color);
    }
}

void SelectionRenderer::AppendManipulators(std::vector<EditorLineVertex>& vertices,
                                           const EditorViewport& viewport) const
{
    ManipulatorVisual visual;
    Session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
    {
        vertices.push_back(EditorLineVertex{ .Position = line.A, .Color = line.Color });
        vertices.push_back(EditorLineVertex{ .Position = line.B, .Color = line.Color });
    }
}
