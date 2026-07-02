#include "EdgeCutTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "document/EdgeCutSettings.h"
#include "document/EditorScene.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElements.h"
#include "overlay/EditorOverlayState.h"
#include "selection/SelectableRef.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridSettings.h"
#include "viewport/Picking.h"
#include "viewport/ViewportProjection.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
// Position 0..1 of the cursor along the edge's screen segment (a -> b), matching
// the a, b convention the cut ops use.
float CursorPositionOnEdge(const EditorViewport& viewport, ImVec2 pos, Vec3d a, Vec3d b)
{
    const ViewportProjection projection(viewport);
    const std::optional<ProjectedPoint> pa = projection.WorldToPixel(a);
    const std::optional<ProjectedPoint> pb = projection.WorldToPixel(b);
    if (!pa.has_value() || !pb.has_value())
        return 0.5f;
    const ImVec2 ab{ pb->Pixel.x - pa->Pixel.x, pb->Pixel.y - pa->Pixel.y };
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1.0e-6f)
        return 0.5f;
    const float s = ((pos.x - pa->Pixel.x) * ab.x + (pos.y - pa->Pixel.y) * ab.y) / len2;
    return std::clamp(s, 0.0f, 1.0f);
}

// Snap the cut position to the world grid along the edge's dominant axis, so a
// grid-snapped cut lands on grid lines. Unchanged for a degenerate edge.
float SnapCutPosition(float t, Vec3d worldA, Vec3d worldB, float spacing)
{
    if (spacing <= 0.0f)
        return t;
    const Vec3d dir = worldB - worldA;
    const float ax = std::abs(dir.X);
    const float ay = std::abs(dir.Y);
    const float az = std::abs(dir.Z);
    const int k = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    const float dk = (k == 0) ? dir.X : (k == 1) ? dir.Y : dir.Z;
    const float ak = (k == 0) ? worldA.X : (k == 1) ? worldA.Y : worldA.Z;
    if (std::abs(dk) < 1.0e-5f)
        return t;
    const float coord = ak + t * dk; // cut point coordinate along axis k
    const float snapped = std::round(coord / spacing) * spacing;
    return std::clamp((snapped - ak) / dk, 0.0f, 1.0f);
}

// The vertex pair (a, b) of the picked face's loop edge nearest the cursor, in the
// face's winding order. No screen threshold: while the cursor is over the face there
// is always a seed edge, so the cut preview doesn't require pixel-perfect aim.
std::optional<std::pair<std::uint32_t, std::uint32_t>> NearestFaceEdge(
    const BrushMesh& mesh, const Transform3f& transform, std::uint32_t faceIndex,
    const EditorViewport& viewport, ImVec2 cursor)
{
    if (faceIndex >= mesh.Faces.size())
        return std::nullopt;
    const std::vector<std::uint32_t>& loop = mesh.Faces[faceIndex].Loop;
    if (loop.size() < 2)
        return std::nullopt;

    const ViewportProjection projection(viewport);
    float best = std::numeric_limits<float>::max();
    std::pair<std::uint32_t, std::uint32_t> result{ loop[0], loop[1] };
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        const std::uint32_t va = loop[i];
        const std::uint32_t vb = loop[(i + 1) % loop.size()];
        const std::optional<ProjectedPoint> pa =
            projection.WorldToPixel(transform.TransformPoint(mesh.Vertices[va].Position));
        const std::optional<ProjectedPoint> pb =
            projection.WorldToPixel(transform.TransformPoint(mesh.Vertices[vb].Position));
        if (!pa.has_value() || !pb.has_value())
            continue;
        const float pixels = ViewportProjection::DistancePointToSegment(cursor, pa->Pixel, pb->Pixel);
        if (pixels < best)
        {
            best = pixels;
            result = { va, vb };
        }
    }
    return result;
}

// The cut's new ring/single edges: every edge of `cut` whose both endpoints are new
// (positions absent from `original` are the split points). The split half-edges have
// one original endpoint and are skipped, so this is just the cut itself.
std::vector<SelectableRef> NewRingEdgeRefs(const EditorScene& scene, EntityId entity,
                                           const BrushMesh& original, const BrushMesh& cut)
{
    std::vector<bool> isNew(cut.Vertices.size(), false);
    for (std::size_t i = 0; i < cut.Vertices.size(); ++i)
    {
        const Vec3d p = cut.Vertices[i].Position;
        bool inOriginal = false;
        for (const BrushVertex& ov : original.Vertices)
            if ((ov.Position - p).SqrMagnitude() < 1.0e-8f) { inOriginal = true; break; }
        isNew[i] = !inOriginal;
    }

    std::vector<SelectableRef> refs;
    const RegistryId registry = scene.GetRegistry().Id;
    for (const EdgeElement& edge : MeshElements::Edges(cut, Transform3f::Identity()))
        if (edge.VertexA < isNew.size() && edge.VertexB < isNew.size()
            && isNew[edge.VertexA] && isNew[edge.VertexB])
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
    return refs;
}
}

std::string_view EdgeCutTool::GetId() const { return "edgecut"; }
std::string_view EdgeCutTool::GetDisplayName() const { return "Edge Cut"; }
std::string_view EdgeCutTool::GetIcon() const { return ICON_FA_SCISSORS; }

InputConsumed EdgeCutTool::OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    UpdatePreview(ctx, viewport, pos);
    return InputConsumed::Yes; // own the hover so the element glow doesn't double up
}

void EdgeCutTool::OnHoverEnd(ToolContext& ctx) { Revert(ctx); }
void EdgeCutTool::OnDeactivate(ToolContext& ctx) { Revert(ctx); }
void EdgeCutTool::OnCancel(ToolContext& ctx) { Revert(ctx); }

InputConsumed EdgeCutTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    UpdatePreview(ctx, viewport, pointer.Position);
    Commit(ctx);
    return InputConsumed::Yes;
}

InputConsumed EdgeCutTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (event.Key == SDLK_TAB)
    {
        ctx.EdgeCut.LoopCut = !ctx.EdgeCut.LoopCut;
        return InputConsumed::Yes;
    }
    return InputConsumed::No;
}

void EdgeCutTool::UpdatePreview(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    // Revert any live preview so the picker sees real meshes, then re-derive.
    if (PreviewEntity.IsValid())
        ctx.Sink.PreviewMesh(PreviewEntity, Original);
    PreviewEntity = {};
    PendingValid = false;
    ctx.Overlay.HoverBody = {};

    // Ray-pick the face under the cursor: it hits anywhere over a brush, so the
    // preview tracks the nearest edge instead of needing pixel-perfect aim.
    const SelectableRef face = ctx.Picking.Pick(viewport, pos, ctx.Scene,
                                                BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!face.IsFace())
    {
        ctx.Overlay.Readout.Clear();
        return;
    }
    ctx.Overlay.HoverBody = face.Entity; // highlight the affected mesh while hovering

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(face.Entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
    {
        ctx.Overlay.Readout.Clear();
        return;
    }

    const BrushMesh& mesh = *resolved->Mesh;
    const std::optional<std::pair<std::uint32_t, std::uint32_t>> seed =
        NearestFaceEdge(mesh, resolved->Transform, face.ElementId, viewport, pos);
    if (!seed.has_value())
    {
        ctx.Overlay.Readout.Clear();
        return;
    }

    const std::uint32_t a = seed->first;
    const std::uint32_t b = seed->second;
    const Vec3d worldA = resolved->Transform.TransformPoint(mesh.Vertices[a].Position);
    const Vec3d worldB = resolved->Transform.TransformPoint(mesh.Vertices[b].Position);
    float t = CursorPositionOnEdge(viewport, pos, worldA, worldB);
    if (ctx.Grid.SnapEnabled)
        t = SnapCutPosition(t, worldA, worldB, ctx.Grid.Spacing); // snaps preview and commit alike

    BrushMesh original = mesh;
    // Single cut splits only the face under the cursor; loop cut rings the edge.
    BrushMesh cut = ctx.EdgeCut.LoopCut
        ? BrushOps::InsertEdgeLoop(original, a, b, t)
        : BrushOps::InsertEdgeCut(original, a, b, t, face.ElementId);

    if (cut.Vertices.size() == original.Vertices.size() || !BrushValidateAndRepair(cut).Ok)
    {
        ctx.Overlay.Readout.Clear();
        return;
    }

    ctx.Sink.PreviewMesh(face.Entity, cut);
    PreviewEntity = face.Entity;
    Original = std::move(original);
    Pending = std::move(cut);
    PendingValid = true;

    const Vec3d cutPoint = worldA * (1.0f - t) + worldB * t;
    DragReadout& readout = ctx.Overlay.Readout;
    readout.From = cutPoint;
    readout.To = cutPoint;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%s %.2f", ctx.EdgeCut.LoopCut ? "loop" : "single", static_cast<double>(t));
    readout.Text = buffer;
    readout.Viewport = viewport.Id;
}

void EdgeCutTool::Commit(ToolContext& ctx)
{
    if (PendingValid && PreviewEntity.IsValid())
    {
        const EntityId entity = PreviewEntity;
        // Clear preview state first: CommitMesh and the tool hand-off below both
        // re-enter OnDeactivate -> Revert, which must be a no-op now (the cut is
        // real, not a preview to roll back).
        PreviewEntity = {};
        PendingValid = false;

        ctx.Sink.CommitMesh(entity, Original, Pending);

        // Select the new ring/single edges and switch to Edge mode + the Select tool,
        // so the new loop is immediately ready to move.
        const std::vector<SelectableRef> newEdges = NewRingEdgeRefs(ctx.Scene, entity, Original, Pending);
        if (!newEdges.empty())
        {
            ctx.Sink.SelectElements(newEdges);
            ctx.MeshEdit.SetElementKind(MeshElementKind::Edge);
        }
        if (ctx.ActivateTool)
            ctx.ActivateTool("select");
    }
    PreviewEntity = {};
    PendingValid = false;
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
}

void EdgeCutTool::Revert(ToolContext& ctx)
{
    if (PreviewEntity.IsValid())
        ctx.Sink.PreviewMesh(PreviewEntity, Original);
    PreviewEntity = {};
    PendingValid = false;
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
}
