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
                                     EditorWideLinePipeline& lines, EditorFillPipeline& fill)
    : Scene(scene)
    , Selection(selection)
    , MeshEdit(meshEdit)
    , Overlay(overlay)
    , Session(session)
    , Lines(lines)
    , Fill(fill)
{
}

void SelectionRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    const std::vector<EntityId> bodies = GatherActiveBodies();
    const bool vertexMode = MeshEdit.GetElementKind() == MeshElementKind::Vertex;

    // The body wireframe and vertex handles are occluded by solid geometry so back
    // edges/handles you can't pick aren't drawn (matching PickEdge/PickVertex, which
    // occlude only in solid shading). In wireframe/ortho they ride the on-top list,
    // since picking doesn't occlude there either.
    const bool occludeBody = viewport.Shading == ViewportShading::Solid;
    std::vector<EditorLineSegment> occluded;
    std::vector<EditorLineSegment> onTop;
    std::vector<EditorLineVertex> faceFill;
    onTop.reserve(selection.size() * 16 + 32);
    std::vector<EditorLineSegment>& bodyLines = occludeBody ? occluded : onTop;

    // Active bodies: the brushes the current selection edits. Bold wireframe (the seam
    // a bloom/glow pass hooks onto) plus, in vertex mode, the grabbable handles.
    for (EntityId entity : bodies)
    {
        const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        AppendWireframe(bodyLines, *mesh, *transform, EditorTheme::ActiveWireframe, EditorTheme::ActiveLinePixels);
        if (vertexMode)
            for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
                AppendVertexSquare(bodyLines, viewport, vertex.Position, EditorTheme::VertexHandle,
                                   EditorTheme::OverlayLinePixels);
    }

    // Preview body: the brush under the cursor a click would make active (edge-cut
    // hover, or another mesh hovered in an element mode). Thin wireframe, no glow and
    // no handles, so it reads as "would be selected" distinct from the active body.
    if (Overlay.HoverBody.IsValid() && Scene.IsEntityVisible(Overlay.HoverBody)
        && std::find(bodies.begin(), bodies.end(), Overlay.HoverBody) == bodies.end())
    {
        const BrushMesh* mesh = Scene.TryGetBrushMesh(Overlay.HoverBody);
        const Transform3f* transform = Scene.TryGetTransform(Overlay.HoverBody);
        if (mesh != nullptr && transform != nullptr)
            AppendWireframe(bodyLines, *mesh, *transform, EditorTheme::PreviewWireframe,
                            EditorTheme::PreviewLinePixels);
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
            {
                AppendFaceFill(faceFill, *face, EditorTheme::FaceFill);
                AppendFace(onTop, *face, EditorTheme::FaceHighlight, EditorTheme::OverlayLinePixels);
            }
        }
        else if (selected.IsEdge())
        {
            if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, selected.ElementId))
                AppendEdge(onTop, *edge, EditorTheme::EdgeHighlight, EditorTheme::OverlayLinePixels);
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
                AppendVertexSquare(onTop, viewport, vertex->Position, EditorTheme::VertexHighlight,
                                   EditorTheme::OverlayLinePixels);
        }
        // object: the active-body wireframe above already covers it.
    }

    AppendHover(onTop, viewport);

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape.
    AppendManipulators(onTop, viewport);

    // Body wireframe/handles depth-test against the scene (back ones cull); selection
    // feedback and gizmos draw on top. The face fill goes down before the on-top
    // strokes so outlines and gizmos read over the translucent quad.
    if (!occluded.empty())
        Lines.Submit(frame, viewport, occluded, /*onTop*/ false);
    if (!faceFill.empty())
        Fill.Submit(frame, viewport, faceFill, /*onTop*/ true);
    Lines.Submit(frame, viewport, onTop, /*onTop*/ true);
}

void SelectionRenderer::SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport)
{
    std::vector<EditorLineSegment> segments;
    for (EntityId entity : GatherActiveBodies())
    {
        const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        AppendWireframe(segments, *mesh, *transform, EditorTheme::ActiveWireframe, EditorTheme::ActiveLinePixels);
    }
    if (!segments.empty())
        Lines.Submit(frame, viewport, segments, /*onTop*/ true);
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

void SelectionRenderer::AppendWireframe(std::vector<EditorLineSegment>& segments,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color,
                                        float widthPx) const
{
    for (const EdgeElement& edge : MeshElements::Edges(mesh, transform))
        segments.push_back(EditorLineSegment{ edge.A, edge.B, color, widthPx });
}

void SelectionRenderer::AppendFace(std::vector<EditorLineSegment>& segments,
                                   const FaceElement& face,
                                   const Vec4& color,
                                   float widthPx) const
{
    for (size_t i = 0; i < face.Corners.size(); ++i)
    {
        const Vec3d& start = face.Corners[i];
        const Vec3d& end = face.Corners[(i + 1) % face.Corners.size()];
        segments.push_back(EditorLineSegment{ start, end, color, widthPx });
    }
}

void SelectionRenderer::AppendFaceFill(std::vector<EditorLineVertex>& triangles,
                                       const FaceElement& face,
                                       const Vec4& color) const
{
    // Brush faces are convex, so a fan from the first corner covers the polygon.
    for (size_t i = 1; i + 1 < face.Corners.size(); ++i)
    {
        triangles.push_back(EditorLineVertex{ face.Corners[0], color });
        triangles.push_back(EditorLineVertex{ face.Corners[i], color });
        triangles.push_back(EditorLineVertex{ face.Corners[i + 1], color });
    }
}

void SelectionRenderer::AppendEdge(std::vector<EditorLineSegment>& segments,
                                   const EdgeElement& edge,
                                   const Vec4& color,
                                   float widthPx) const
{
    segments.push_back(EditorLineSegment{ edge.A, edge.B, color, widthPx });
}

void SelectionRenderer::AppendVertexSquare(std::vector<EditorLineSegment>& segments,
                                           const EditorViewport& viewport,
                                           Vec3d position,
                                           const Vec4& color,
                                           float widthPx) const
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
        segments.push_back(EditorLineSegment{ corners[i], corners[(i + 1) % corners.size()], color, widthPx });
}

void SelectionRenderer::AppendHover(std::vector<EditorLineSegment>& segments, const EditorViewport& viewport) const
{
    const SelectableRef hovered = Overlay.Hover.Element;
    if (!hovered.IsValid() || hovered.Registry != Scene.GetRegistry().Id)
        return;
    if (!Scene.IsEntityVisible(hovered.Entity))
        return;

    // An already-selected element keeps its selection color; painting the hover
    // tint over it would mask the selected state under the cursor.
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    if (std::find(selection.begin(), selection.end(), hovered) != selection.end())
        return;

    const BrushMesh* mesh = Scene.TryGetBrushMesh(hovered.Entity);
    const Transform3f* transform = Scene.TryGetTransform(hovered.Entity);
    if (mesh == nullptr || transform == nullptr)
        return;

    const Vec4 color = EditorTheme::HoverEligible;
    const float width = EditorTheme::OverlayLinePixels;
    if (hovered.IsFace())
    {
        if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, hovered.ElementId))
            AppendFace(segments, *face, color, width);
    }
    else if (hovered.IsEdge())
    {
        if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, hovered.ElementId))
            AppendEdge(segments, *edge, color, width);
    }
    else if (hovered.IsVertex())
    {
        if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, hovered.ElementId))
            AppendVertexSquare(segments, viewport, vertex->Position, color, width);
    }
    else // object: glow its wireframe so you see what a click would select
    {
        AppendWireframe(segments, *mesh, *transform, color, width);
    }
}

void SelectionRenderer::AppendManipulators(std::vector<EditorLineSegment>& segments,
                                           const EditorViewport& viewport) const
{
    ManipulatorVisual visual;
    Session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
        segments.push_back(EditorLineSegment{ line.A, line.B, line.Color, EditorTheme::OverlayLinePixels });
}
