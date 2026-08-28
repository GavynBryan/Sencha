#include "SelectionRenderer.h"

#include "EditorTheme.h"
#include "brush/BrushTessellation.h"
#include "editmodes/ManipulatorSession.h"
#include "meshedit/MeshEditService.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/ViewportProjection.h"

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

SelectionRenderer::SelectionRenderer(SelectionService& selection, MeshEditService& meshEdit,
                                     const EditorOverlayState& overlay,
                                     EditorWideLinePipeline& lines, EditorFillPipeline& fill)
    : Selection(selection)
    , MeshEdit(meshEdit)
    , Overlay(overlay)
    , Lines(lines)
    , Fill(fill)
{
}

void SelectionRenderer::BeginFrame()
{
    EdgeCache.clear();
}

const std::vector<EdgeElement>& SelectionRenderer::EdgesFor(EntityId entity,
                                                            const BrushMesh& mesh,
                                                            const Transform3f& transform)
{
    const std::uint64_t key = (static_cast<std::uint64_t>(entity.Index) << 32)
                            | static_cast<std::uint64_t>(entity.Generation);
    const auto it = EdgeCache.find(key);
    if (it != EdgeCache.end())
        return it->second;
    return EdgeCache.emplace(key, MeshElements::Edges(mesh, transform)).first->second;
}

void SelectionRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                     const CameraRenderData& camera,
                                     const EditorScene& scene, const ManipulatorSession& session)
{
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    const std::vector<EntityId> bodies = GatherActiveBodies(scene);
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
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        AppendWireframe(bodyLines, *mesh, *transform, entity, EditorTheme::ActiveWireframe,
                        EditorTheme::ActiveLinePixels);
        if (vertexMode)
            for (const VertexElement& vertex : MeshElements::Vertices(*mesh, *transform))
                AppendVertexSquare(bodyLines, viewport, vertex.Position, EditorTheme::VertexHandle,
                                   EditorTheme::OverlayLinePixels);
    }

    // Preview body: the brush under the cursor a click would make active (edge-cut
    // hover, or another mesh hovered in an element mode). Thin wireframe, no glow and
    // no handles, so it reads as "would be selected" distinct from the active body.
    if (Overlay.HoverBody.IsValid() && scene.IsEntityVisible(Overlay.HoverBody)
        && std::find(bodies.begin(), bodies.end(), Overlay.HoverBody) == bodies.end())
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(Overlay.HoverBody);
        const Transform3f* transform = scene.TryGetWorldTransform(Overlay.HoverBody);
        if (mesh != nullptr && transform != nullptr)
            AppendWireframe(bodyLines, *mesh, *transform, Overlay.HoverBody,
                            EditorTheme::PreviewWireframe, EditorTheme::PreviewLinePixels);
    }

    // Per-element highlights, the hover glow, and the gizmos stay on top so the
    // selection and manipulators read through geometry.
    for (SelectableRef selected : selection)
    {
        if (!selected.IsValid() || selected.Registry != scene.GetRegistry().Id)
            continue;
        if (!scene.IsEntityVisible(selected.Entity))
            continue;

        const BrushMesh* mesh = scene.TryGetBrushMesh(selected.Entity);
        const Transform3f* transform = scene.TryGetWorldTransform(selected.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        if (selected.IsFace())
        {
            if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, selected.ElementId))
            {
                AppendFaceFill(faceFill, *mesh, *transform, selected.ElementId, EditorTheme::FaceFill);
                AppendFace(onTop, *face, EditorTheme::FaceHighlight, EditorTheme::OverlayLinePixels);
            }
        }
        else if (selected.IsEdge())
        {
            const std::vector<EdgeElement>& edges = EdgesFor(selected.Entity, *mesh, *transform);
            if (selected.ElementId < edges.size())
                AppendEdge(onTop, edges[selected.ElementId], EditorTheme::EdgeHighlight,
                           EditorTheme::OverlayLinePixels);
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
                AppendVertexSquare(onTop, viewport, vertex->Position, EditorTheme::VertexHighlight,
                                   EditorTheme::OverlayLinePixels);
        }
        // object: the active-body wireframe above already covers it.
    }

    AppendHover(onTop, viewport, scene);

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape.
    AppendManipulators(onTop, viewport, session);

    // Body wireframe/handles depth-test against the scene (back ones cull); selection
    // feedback and gizmos draw on top. The face fill goes down before the on-top
    // strokes so outlines and gizmos read over the translucent quad.
    if (!occluded.empty())
        Lines.Submit(frame, viewport, camera, occluded, /*onTop*/ false, "SelectionRenderer.occluded");
    if (!faceFill.empty())
        Fill.Submit(frame, viewport, camera, faceFill, /*onTop*/ true);
    Lines.Submit(frame, viewport, camera, onTop, /*onTop*/ true, "SelectionRenderer.onTop");
}

void SelectionRenderer::SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport,
                                               const CameraRenderData& camera,
                                               const EditorScene& scene)
{
    std::vector<EditorLineSegment> segments;
    for (EntityId entity : GatherActiveBodies(scene))
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (mesh == nullptr || transform == nullptr)
            continue;
        AppendWireframe(segments, *mesh, *transform, entity, EditorTheme::ActiveWireframe,
                        EditorTheme::ActiveLinePixels);
    }
    if (!segments.empty())
        Lines.Submit(frame, viewport, camera, segments, /*onTop*/ true, "SelectionRenderer.activeGlow");
}

std::vector<EntityId> SelectionRenderer::GatherActiveBodies(const EditorScene& scene) const
{
    std::vector<EntityId> bodies;
    for (SelectableRef ref : Selection.GetSelection())
    {
        if (!ref.IsValid() || ref.Registry != scene.GetRegistry().Id || !ref.Entity.IsValid())
            continue;
        if (!scene.IsEntityVisible(ref.Entity))
            continue;
        if (std::find(bodies.begin(), bodies.end(), ref.Entity) == bodies.end())
            bodies.push_back(ref.Entity);
    }
    return bodies;
}

void SelectionRenderer::AppendWireframe(std::vector<EditorLineSegment>& segments,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        EntityId entity,
                                        const Vec4& color,
                                        float widthPx)
{
    for (const EdgeElement& edge : EdgesFor(entity, mesh, transform))
    {
        const Vec4& stroke = !mesh.SoftEdges.empty()
                && BrushEdgeIsSoft(mesh, edge.VertexA, edge.VertexB)
            ? EditorTheme::SoftEdgeWireframe
            : color;
        segments.push_back(EditorLineSegment{ edge.A, edge.B, stroke, widthPx });
    }
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
                                       const BrushMesh& mesh,
                                       const Transform3f& transform,
                                       std::uint32_t faceIndex,
                                       const Vec4& color) const
{
    BrushTessellateFace(mesh, transform, faceIndex,
        [&](std::uint32_t, const FaceMaterial&, std::span<const BrushTriVertex> tris) {
            for (const BrushTriVertex& tri : tris)
                triangles.push_back(EditorLineVertex{ tri.Position, color });
        });
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

void SelectionRenderer::AppendHover(std::vector<EditorLineSegment>& segments, const EditorViewport& viewport,
                                    const EditorScene& scene)
{
    const SelectableRef hovered = Overlay.Hover.Element;
    if (!hovered.IsValid() || hovered.Registry != scene.GetRegistry().Id)
        return;
    if (!scene.IsEntityVisible(hovered.Entity))
        return;

    // An already-selected element keeps its selection color; painting the hover
    // tint over it would mask the selected state under the cursor.
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    if (std::find(selection.begin(), selection.end(), hovered) != selection.end())
        return;

    const BrushMesh* mesh = scene.TryGetBrushMesh(hovered.Entity);
    const Transform3f* transform = scene.TryGetWorldTransform(hovered.Entity);
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
        const std::vector<EdgeElement>& edges = EdgesFor(hovered.Entity, *mesh, *transform);
        if (hovered.ElementId < edges.size())
            AppendEdge(segments, edges[hovered.ElementId], color, width);
    }
    else if (hovered.IsVertex())
    {
        if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, hovered.ElementId))
            AppendVertexSquare(segments, viewport, vertex->Position, color, width);
    }
    else // object: glow its wireframe so you see what a click would select
    {
        AppendWireframe(segments, *mesh, *transform, hovered.Entity, color, width);
    }
}

void SelectionRenderer::AppendManipulators(std::vector<EditorLineSegment>& segments,
                                           const EditorViewport& viewport,
                                           const ManipulatorSession& session) const
{
    ManipulatorVisual visual;
    session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
        segments.push_back(EditorLineSegment{ line.A, line.B, line.Color, EditorTheme::OverlayLinePixels });
}
