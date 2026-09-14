#include "SelectionRenderer.h"

#include "brush/BrushEvaluation.h"
#include "brush/BrushModifier.h"

#include "EditorInstancedFillPipeline.h"
#include "EditorInstancedLinePipeline.h"
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

void SelectionRenderer::SetInstancing(const BrushDrawSet* draws, EditorInstancedLinePipeline* lines,
                                      EditorInstancedFillPipeline* fill)
{
    Draws = draws;
    InstancedLines = lines;
    InstancedFill = fill;
    Bodies.clear();
    FaceHighlights.clear();
}

void SelectionRenderer::BeginFrame()
{
    ++FrameCounter;
    std::erase_if(Bodies, [&](const auto& entry) { return entry.second.LastUsedFrame + 1 < FrameCounter; });
    std::erase_if(FaceHighlights, [&](const auto& entry) { return entry.second.LastUsedFrame + 1 < FrameCounter; });
}

namespace
{
    std::uint64_t EntityKey(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation;
    }

    // The run's instance rows with the source placement left out, as up to two
    // contiguous spans: the source piece draws wide, never twice.
    template <class F>
    void ForEachCopySpan(const BrushDrawEntity& record, const BrushMeshRun& run, F&& fn)
    {
        const std::uint32_t begin = run.FirstPlacement;
        const std::uint32_t end = run.FirstPlacement + run.PlacementCount;
        if (record.SourceSlot < begin || record.SourceSlot >= end)
        {
            fn(begin, end);
            return;
        }
        if (record.SourceSlot > begin)
            fn(begin, record.SourceSlot);
        if (record.SourceSlot + 1 < end)
            fn(record.SourceSlot + 1, end);
    }

    std::span<const EditorLineInstance> RowsOf(const BrushDrawEntity& record, std::uint32_t begin, std::uint32_t end)
    {
        return std::span<const EditorLineInstance>(
            reinterpret_cast<const EditorLineInstance*>(record.InstanceRows.data() + begin), end - begin);
    }
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
    std::vector<InstancedBody> copies;
    std::vector<InstancedFace> instancedFaces;

    // Active bodies: the brushes the current selection edits. Bold wireframe (the seam
    // a bloom/glow pass hooks onto) plus, in vertex mode, the grabbable handles.
    for (EntityId entity : bodies)
    {
        // The whole evaluated result is the body; handles sit on the source
        // alone, since that is what a drag edits.
        AppendBodyWireframe(bodyLines, scene, entity, EditorTheme::ActiveWireframe,
                            EditorTheme::ActiveLinePixels, copies);
        AppendMirrorPlanes(onTop, scene, entity);
        AppendArrayExtent(onTop, scene, entity);
        const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(entity);
        if (elements == nullptr)
            continue;
        if (vertexMode)
            for (const VertexElement& vertex : elements->Vertices)
                AppendVertexSquare(bodyLines, viewport, vertex.Position, EditorTheme::VertexHandle,
                                   EditorTheme::OverlayLinePixels);
    }

    // Preview body: the brush under the cursor a click would make active (edge-cut
    // hover, or another mesh hovered in an element mode). Thin wireframe, no glow and
    // no handles, so it reads as "would be selected" distinct from the active body.
    if (Overlay.HoverBody.IsValid() && scene.IsEntityEffectivelyVisible(Overlay.HoverBody)
        && std::find(bodies.begin(), bodies.end(), Overlay.HoverBody) == bodies.end())
    {
        AppendBodyWireframe(bodyLines, scene, Overlay.HoverBody,
                            EditorTheme::PreviewWireframe, EditorTheme::PreviewLinePixels, copies);
    }

    // Per-element highlights, the hover glow, and the gizmos stay on top so the
    // selection and manipulators read through geometry.
    for (SelectableRef selected : selection)
    {
        if (!selected.IsValid() || selected.Registry != scene.GetRegistry().Id)
            continue;
        if (!scene.IsEntityEffectivelyVisible(selected.Entity))
            continue;

        const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(selected.Entity);
        if (elements == nullptr)
            continue;

        if (selected.IsFace())
        {
            // A selected face lights up on every copy: the visible tie between
            // generated geometry and the source face an edit lands on.
            AppendFaceHighlight(onTop, faceFill, scene, selected.Entity, selected.ElementId,
                                EditorTheme::FaceHighlight, EditorTheme::OverlayLinePixels, instancedFaces);
        }
        else if (selected.IsEdge())
        {
            if (selected.ElementId < elements->Edges.size())
                AppendEdge(onTop, elements->Edges[selected.ElementId], EditorTheme::EdgeHighlight,
                           EditorTheme::OverlayLinePixels);
        }
        else if (selected.IsVertex())
        {
            if (selected.ElementId < elements->Vertices.size())
                AppendVertexSquare(onTop, viewport, elements->Vertices[selected.ElementId].Position,
                                   EditorTheme::VertexHighlight, EditorTheme::OverlayLinePixels);
        }
        // object: the active-body wireframe above already covers it.
    }

    AppendHover(onTop, viewport, scene, copies);

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape.
    AppendManipulators(onTop, viewport, session);

    // Body wireframe/handles depth-test against the scene (back ones cull); selection
    // feedback and gizmos draw on top. The face fill goes down before the on-top
    // strokes so outlines and gizmos read over the translucent quad.
    if (!occluded.empty())
        Lines.Submit(frame, viewport, camera, occluded, /*onTop*/ false, "SelectionRenderer.occluded");
    SubmitInstancedCopies(frame, viewport, camera, copies, /*onTop*/ !occludeBody);
    if (!faceFill.empty())
        Fill.Submit(frame, viewport, camera, faceFill, /*onTop*/ true);
    SubmitInstancedFaces(frame, viewport, camera, instancedFaces, /*onTop*/ true);
    Lines.Submit(frame, viewport, camera, onTop, /*onTop*/ true, "SelectionRenderer.onTop");
}

void SelectionRenderer::SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport,
                                               const CameraRenderData& camera,
                                               const EditorScene& scene)
{
    std::vector<EditorLineSegment> segments;
    std::vector<InstancedBody> copies;
    for (EntityId entity : GatherActiveBodies(scene))
        AppendBodyWireframe(segments, scene, entity, EditorTheme::ActiveWireframe,
                            EditorTheme::ActiveLinePixels, copies);
    if (!segments.empty())
        Lines.Submit(frame, viewport, camera, segments, /*onTop*/ true, "SelectionRenderer.activeGlow");
    SubmitInstancedCopies(frame, viewport, camera, copies, /*onTop*/ true);
}

std::vector<EntityId> SelectionRenderer::GatherActiveBodies(const EditorScene& scene) const
{
    std::vector<EntityId> bodies;
    for (SelectableRef ref : Selection.GetSelection())
    {
        if (!ref.IsValid() || ref.Registry != scene.GetRegistry().Id || !ref.Entity.IsValid())
            continue;
        if (!scene.IsEntityEffectivelyVisible(ref.Entity))
            continue;
        if (std::find(bodies.begin(), bodies.end(), ref.Entity) == bodies.end())
            bodies.push_back(ref.Entity);
    }
    return bodies;
}

void SelectionRenderer::AppendBodyWireframe(std::vector<EditorLineSegment>& segments,
                                            const EditorScene& scene,
                                            EntityId entity,
                                            const Vec4& color,
                                            float widthPx,
                                            std::vector<InstancedBody>& copies)
{
    const BrushDrawEntity* record = Draws != nullptr && InstancedLines != nullptr ? Draws->Find(entity) : nullptr;
    const bool sourceOnly = record != nullptr && record->Placements.size() > 1;
    if (sourceOnly)
        copies.push_back(InstancedBody{ record, color });
    AppendBodyWireframeAllPieces(segments, scene, entity, color, widthPx, sourceOnly);
}

void SelectionRenderer::SubmitInstancedCopies(const FrameContext& frame, const EditorViewport& viewport,
                                              const CameraRenderData& camera,
                                              std::span<const InstancedBody> copies, bool onTop)
{
    if (InstancedLines == nullptr)
        return;
    for (const InstancedBody& body : copies)
    {
        for (const BrushMeshRun& run : body.Record->Runs)
        {
            const BrushDrawMesh& mesh = body.Record->Meshes[run.MeshIndex];
            if (mesh.Edges.empty())
                continue;
            InstanceScratch.clear();
            InstanceScratch.reserve(mesh.Edges.size());
            for (const BrushEdgeVertex& edge : mesh.Edges)
                InstanceScratch.push_back(EditorLineVertex{ .Position = edge.Position, .Color = body.Color });
            ForEachCopySpan(*body.Record, run, [&](std::uint32_t begin, std::uint32_t end)
            {
                InstancedLines->Submit(frame, viewport, camera, InstanceScratch,
                                       RowsOf(*body.Record, begin, end), onTop);
            });
        }
    }
}

void SelectionRenderer::AppendBodyWireframeAllPieces(std::vector<EditorLineSegment>& segments,
                                                     const EditorScene& scene,
                                                     EntityId entity,
                                                     const Vec4& color,
                                                     float widthPx,
                                                     bool sourceOnly)
{
    const BrushEvaluated* evaluated = scene.PlacementFacts().GetEvaluation(entity);
    const std::optional<BrushPlacementKey> key = scene.PlacementFacts().KeyOf(entity);
    if (evaluated == nullptr || !key.has_value())
        return;

    RetainedBody& body = Bodies[(static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation];
    if (body.LastUsedFrame == 0 || !(body.Key == *key) || body.Color != color || body.WidthPx != widthPx
        || body.SourceOnly != sourceOnly)
    {
        body.Key = *key;
        body.Color = color;
        body.WidthPx = widthPx;
        body.SourceOnly = sourceOnly;
        body.Segments.clear();
        const std::span<const Transform3f> placements = scene.PlacementFacts().GetPiecePlacements(entity);
        for (const BrushPiece& piece : evaluated->Pieces)
        {
            if (sourceOnly && piece.Origin != BrushPieceOrigin::Source)
                continue;
            if (piece.Ordinal < placements.size())
                AppendWireframe(body.Segments, evaluated->Meshes[piece.MeshIndex], placements[piece.Ordinal],
                                color, widthPx);
        }
    }
    body.LastUsedFrame = FrameCounter;
    segments.insert(segments.end(), body.Segments.begin(), body.Segments.end());
}

void SelectionRenderer::AppendFaceHighlight(std::vector<EditorLineSegment>& outline,
                                            std::vector<EditorLineVertex>& fill,
                                            const EditorScene& scene,
                                            EntityId entity,
                                            std::uint32_t sourceFace,
                                            const Vec4& outlineColor,
                                            float widthPx,
                                            std::vector<InstancedFace>& instanced)
{
    const BrushPlacementFacts& facts = scene.PlacementFacts();
    const BrushEvaluated* evaluated = facts.GetEvaluation(entity);
    const SourceWorldElements* elements = facts.GetSourceWorldElements(entity);
    const std::optional<BrushPlacementKey> key = facts.KeyOf(entity);
    if (evaluated == nullptr || elements == nullptr || !key.has_value())
        return;

    // The source face itself: wide outline from the retained world elements.
    if (sourceFace < elements->Faces.size())
        AppendFace(outline, elements->Faces[sourceFace], outlineColor, widthPx);

    const BrushDrawEntity* record =
        Draws != nullptr && InstancedFill != nullptr && InstancedLines != nullptr ? Draws->Find(entity) : nullptr;
    if (record != nullptr)
    {
        // Resolved per source face x distinct mesh, instanced over placements.
        instanced.push_back(InstancedFace{ record, &FaceHighlightFor(entity, sourceFace, *evaluated,
                                                                     key->TopologyRevision) });
        return;
    }

    // Without instancing: fill and outline every placement of every evaluated
    // face that maps to the source face, from the retained placements.
    const std::span<const Transform3f> placements = facts.GetPiecePlacements(entity);
    for (const BrushPiece& piece : evaluated->Pieces)
    {
        if (piece.Ordinal >= placements.size())
            continue;
        const Transform3f& world = placements[piece.Ordinal];
        const BrushEvaluatedMesh& mesh = evaluated->Meshes[piece.MeshIndex];
        const auto light = [&](std::uint32_t f)
        {
            if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*piece.Mesh, world, f))
            {
                AppendFaceFill(fill, *piece.Mesh, world, f, EditorTheme::FaceFill);
                if (piece.Origin != BrushPieceOrigin::Source)
                    AppendFace(outline, *face, outlineColor, widthPx);
            }
        };
        if (mesh.ToSource.Faces == BrushElementMapKind::Identity)
            light(sourceFace);
        else if (mesh.ToSource.Faces == BrushElementMapKind::Table)
            for (std::uint32_t f = 0; f < mesh.ToSource.FaceTable.size(); ++f)
                if (mesh.ToSource.FaceTable[f] == sourceFace)
                    light(f);
    }
}

const BrushFaceHighlight& SelectionRenderer::FaceHighlightFor(EntityId entity, std::uint32_t sourceFace,
                                                              const BrushEvaluated& evaluated,
                                                              std::uint64_t topologyRevision)
{
    RetainedFace& retained = FaceHighlights[EntityKey(entity) ^ (static_cast<std::uint64_t>(sourceFace) * 0x9E3779B97F4A7C15ull)];
    if (retained.LastUsedFrame == 0 || retained.Geometry.TopologyRevision != topologyRevision
        || retained.Geometry.SourceFace != sourceFace)
        retained.Geometry = BuildBrushFaceHighlight(evaluated, sourceFace, topologyRevision);
    retained.LastUsedFrame = FrameCounter;
    return retained.Geometry;
}

void SelectionRenderer::SubmitInstancedFaces(const FrameContext& frame, const EditorViewport& viewport,
                                             const CameraRenderData& camera,
                                             std::span<const InstancedFace> faces, bool onTop)
{
    if (InstancedFill == nullptr || InstancedLines == nullptr)
        return;
    for (const InstancedFace& face : faces)
    {
        for (const BrushFaceHighlightMesh& geometry : face.Geometry->Meshes)
        {
            for (const BrushMeshRun& run : face.Record->Runs)
            {
                if (run.MeshIndex != geometry.MeshIndex)
                    continue;
                // Fill on every placement, the source included.
                InstanceScratch.clear();
                InstanceScratch.reserve(geometry.Fill.size());
                for (const Vec3d& position : geometry.Fill)
                    InstanceScratch.push_back(EditorLineVertex{ .Position = position, .Color = EditorTheme::FaceFill });
                InstancedFill->Submit(frame, viewport, camera, InstanceScratch,
                                      RowsOf(*face.Record, run.FirstPlacement, run.FirstPlacement + run.PlacementCount),
                                      onTop);
                // Thin outline on the copies; the source keeps its wide stroke.
                InstanceScratch.clear();
                InstanceScratch.reserve(geometry.Outline.size());
                for (const Vec3d& position : geometry.Outline)
                    InstanceScratch.push_back(EditorLineVertex{ .Position = position, .Color = EditorTheme::FaceHighlight });
                ForEachCopySpan(*face.Record, run, [&](std::uint32_t begin, std::uint32_t end)
                {
                    InstancedLines->Submit(frame, viewport, camera, InstanceScratch,
                                           RowsOf(*face.Record, begin, end), onTop);
                });
            }
        }
    }
}

void SelectionRenderer::AppendMirrorPlanes(std::vector<EditorLineSegment>& segments,
                                           const EditorScene& scene,
                                           EntityId entity) const
{
    // Drawn from what evaluation resolved, never from stored parameters, so the
    // plane shown is the plane used: an Origin mirror sits on the brush origin
    // (the entity transform), not on the transient pivot.
    const BrushModifierStack* modifiers = scene.TryGetBrushModifiers(entity);
    const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity);
    const Transform3f* transform = scene.TryGetWorldTransform(entity);
    if (modifiers == nullptr || evaluated == nullptr || transform == nullptr)
        return;
    for (std::size_t i = 0; i < modifiers->size() && i < evaluated->Stages.size(); ++i)
    {
        const BrushStageResolution& stage = evaluated->Stages[i];
        if (!stage.Applied || !std::holds_alternative<MirrorModifier>((*modifiers)[i].Params))
            continue;
        const Plane& plane = stage.MirrorPlane; // normalized by the evaluator
        const Aabb3d bounds = stage.InputBounds.IsValid() ? stage.InputBounds : Aabb3d{};
        const float half = std::max(0.5f, (bounds.Max - bounds.Min).Magnitude() * 0.55f);
        const Vec3d center = plane.ClosestPoint(bounds.IsValid() ? bounds.Center() : Vec3d{});
        const Vec3d reference = std::abs(plane.Normal.Y) < 0.9f ? Vec3d{ 0, 1, 0 } : Vec3d{ 1, 0, 0 };
        const Vec3d u = plane.Normal.Cross(reference).Normalized();
        const Vec3d v = plane.Normal.Cross(u).Normalized();
        const std::array<Vec3d, 4> corners = {
            transform->TransformPoint(center + u * half + v * half),
            transform->TransformPoint(center - u * half + v * half),
            transform->TransformPoint(center - u * half - v * half),
            transform->TransformPoint(center + u * half - v * half),
        };
        for (std::size_t c = 0; c < corners.size(); ++c)
            segments.push_back(EditorLineSegment{ corners[c], corners[(c + 1) % corners.size()],
                                                  EditorTheme::PreviewWireframe,
                                                  EditorTheme::PreviewLinePixels });
        // A short arrow along the mirror axis, both ways: the copy lies across it.
        const float reach = half * 0.4f;
        const Vec3d a = transform->TransformPoint(center - plane.Normal * reach);
        const Vec3d b = transform->TransformPoint(center + plane.Normal * reach);
        segments.push_back(EditorLineSegment{ a, b, EditorTheme::PreviewWireframe,
                                              EditorTheme::PreviewLinePixels });
    }
}

void SelectionRenderer::AppendArrayExtent(std::vector<EditorLineSegment>& segments,
                                          const EditorScene& scene,
                                          EntityId entity) const
{
    // From the input set the array repeats (after a Mirror, the mirrored pair)
    // to the last copy, one tick per copy.
    const BrushModifierStack* modifiers = scene.TryGetBrushModifiers(entity);
    const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity);
    const Transform3f* transform = scene.TryGetWorldTransform(entity);
    if (modifiers == nullptr || evaluated == nullptr || transform == nullptr)
        return;
    for (std::size_t i = 0; i < modifiers->size() && i < evaluated->Stages.size(); ++i)
    {
        const BrushStageResolution& stage = evaluated->Stages[i];
        const auto* array = std::get_if<ArrayModifier>(&(*modifiers)[i].Params);
        if (array == nullptr || !stage.Applied || !stage.InputBounds.IsValid())
            continue;
        const int count = std::max(1, array->Count);
        const Vec3d start = stage.InputBounds.Center();
        const Vec3d step = stage.ArrayStep;
        const Vec3d end = start + step * static_cast<float>(count - 1);
        segments.push_back(EditorLineSegment{ transform->TransformPoint(start),
                                              transform->TransformPoint(end),
                                              EditorTheme::PreviewWireframe,
                                              EditorTheme::PreviewLinePixels });
        const Vec3d direction = step.SqrMagnitude() > 1e-12f ? step.Normalized() : Vec3d{ 1, 0, 0 };
        const Vec3d reference = std::abs(direction.Y) < 0.9f ? Vec3d{ 0, 1, 0 } : Vec3d{ 1, 0, 0 };
        const Vec3d across = direction.Cross(reference).Normalized();
        const float tick = std::max(0.1f, (stage.InputBounds.Max - stage.InputBounds.Min).Magnitude() * 0.1f);
        for (int c = 0; c < count; ++c)
        {
            const Vec3d at = start + step * static_cast<float>(c);
            segments.push_back(EditorLineSegment{ transform->TransformPoint(at - across * tick),
                                                  transform->TransformPoint(at + across * tick),
                                                  EditorTheme::PreviewWireframe,
                                                  EditorTheme::PreviewLinePixels });
        }
    }
}

void SelectionRenderer::AppendWireframe(std::vector<EditorLineSegment>& segments,
                                        const BrushEvaluatedMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color,
                                        float widthPx)
{
    for (std::size_t e = 0; e < mesh.EdgePairs.size(); ++e)
    {
        const auto& pair = mesh.EdgePairs[e];
        const Vec4& stroke = e < mesh.EdgeSoft.size() && mesh.EdgeSoft[e]
            ? EditorTheme::SoftEdgeWireframe
            : color;
        segments.push_back(EditorLineSegment{ transform.TransformPoint(mesh.Mesh->Vertices[pair[0]].Position),
                                              transform.TransformPoint(mesh.Mesh->Vertices[pair[1]].Position),
                                              stroke, widthPx });
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
                                    const EditorScene& scene, std::vector<InstancedBody>& copies)
{
    const SelectableRef hovered = Overlay.Hover.Element;
    if (!hovered.IsValid() || hovered.Registry != scene.GetRegistry().Id)
        return;
    if (!scene.IsEntityEffectivelyVisible(hovered.Entity))
        return;

    // An already-selected element keeps its selection color; painting the hover
    // tint over it would mask the selected state under the cursor.
    const std::span<const SelectableRef> selection = Selection.GetSelection();
    if (std::find(selection.begin(), selection.end(), hovered) != selection.end())
        return;

    const SourceWorldElements* elements = scene.PlacementFacts().GetSourceWorldElements(hovered.Entity);
    if (elements == nullptr)
        return;

    const Vec4 color = EditorTheme::HoverEligible;
    const float width = EditorTheme::HoverLinePixels;
    if (hovered.IsFace())
    {
        if (hovered.ElementId < elements->Faces.size())
            AppendFace(segments, elements->Faces[hovered.ElementId], color, width);
    }
    else if (hovered.IsEdge())
    {
        if (hovered.ElementId < elements->Edges.size())
            AppendEdge(segments, elements->Edges[hovered.ElementId], color, width);
    }
    else if (hovered.IsVertex())
    {
        if (hovered.ElementId < elements->Vertices.size())
            AppendVertexSquare(segments, viewport, elements->Vertices[hovered.ElementId].Position, color, width);
    }
    else // object: glow its wireframe so you see what a click would select
    {
        AppendBodyWireframe(segments, scene, hovered.Entity, color, width, copies);
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
