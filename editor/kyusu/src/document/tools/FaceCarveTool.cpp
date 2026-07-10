#include "FaceCarveTool.h"

#include "EditorTheme.h"
#include "fonts/IconsFontAwesome6.h"

#include "document/EditorScene.h"
#include "brush/BrushTransform.h"
#include "brush/BrushValidation.h"
#include "commands/CommandStack.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshElements.h"
#include "meshedit/MeshEditService.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "selection/SelectableRef.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"
#include "viewport/ViewportProjection.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr float kAdjustHandleHitPixels = 11.0f;

float ScreenDistance(ImVec2 a, ImVec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<SelectableRef> NewEdgeRefs(const EditorScene& scene, EntityId entity,
                                       const BrushMesh& original, const BrushMesh& edited)
{
    std::vector<bool> isNew(edited.Vertices.size(), false);
    for (std::size_t i = 0; i < edited.Vertices.size(); ++i)
    {
        const Vec3d p = edited.Vertices[i].Position;
        bool inOriginal = false;
        for (const BrushVertex& ov : original.Vertices)
        {
            if ((ov.Position - p).SqrMagnitude() <= 1.0e-8f)
            {
                inOriginal = true;
                break;
            }
        }
        isNew[i] = !inOriginal;
    }

    std::vector<SelectableRef> refs;
    const RegistryId registry = scene.GetRegistry().Id;
    for (const EdgeElement& edge : MeshElements::Edges(edited, Transform3f::Identity()))
    {
        if (edge.VertexA < isNew.size() && edge.VertexB < isNew.size()
            && isNew[edge.VertexA] && isNew[edge.VertexB])
            refs.push_back(SelectableRef::EdgeSelection(registry, entity, edge.Index));
    }
    return refs;
}

// Forwards the drag to the tool. The ToolRegistry owns the tool and the
// InteractionHost cancels interactions before any tool switch, so the reference
// cannot dangle.
class CarveDragInteraction : public IInteraction
{
public:
    explicit CarveDragInteraction(FaceCarveTool& tool) : Tool(tool) {}

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position);
    }

    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override
    {
        Tool.UpdateDrag(ctx, viewport, pointer.Position);
        Tool.EndDrag(ctx);
    }

    void OnCancel(ToolContext& ctx) override { Tool.RevertAll(ctx); }

private:
    FaceCarveTool& Tool;
};
}

std::string_view FaceCarveTool::GetId() const
{
    return "facecarve";
}

std::string_view FaceCarveTool::GetDisplayName() const
{
    return "Face Carve";
}

std::string_view FaceCarveTool::GetIcon() const
{
    return ICON_FA_CROP_SIMPLE;
}

void FaceCarveTool::SetMode(ToolContext& ctx, FaceCarveMode mode)
{
    if (Mode == mode)
        return;
    RevertAll(ctx);
    Mode = mode;
}

void FaceCarveTool::SetPierceEnabled(ToolContext& ctx, bool enabled)
{
    if (Pierce == enabled)
        return;
    Pierce = enabled;
    if (Phase == FaceCarvePhase::Idle)
        return;

    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };
    if (Mode == FaceCarveMode::EdgeLoop)
        UpdateLoopPreview(ctx, rectMin, rectMax);
    else
        UpdateRectPreview(ctx, rectMin, rectMax);
    WriteReadout(ctx, rectMin, rectMax, HasPending());
}

InputConsumed FaceCarveTool::OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    // A pending carve keeps its own glow and readout; hover changes nothing.
    if (Phase != FaceCarvePhase::Idle)
    {
        ctx.Overlay.HoverBody = TargetEntity;
        return InputConsumed::Yes;
    }

    const SelectableRef picked = ctx.Picking.Pick(viewport, pos, ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
    {
        ctx.Overlay.HoverBody = {};
        return InputConsumed::Yes;
    }

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(picked.Entity);
    const bool valid = resolved.has_value() && resolved->Mesh != nullptr
        && BrushOps::RectFaceFrame(*resolved->Mesh, picked.ElementId).has_value();

    // Tools have no logging service; the reject reason surfaces as a readout
    // right where the user is pointing.
    ctx.Overlay.HoverBody = valid ? picked.Entity : EntityId{};
    if (!valid && resolved.has_value() && resolved->Mesh != nullptr)
    {
        const auto face = MeshElements::TryGetFace(*resolved->Mesh, resolved->Transform, picked.ElementId);
        if (face.has_value())
        {
            DragReadout& readout = ctx.Overlay.Readout;
            readout.From = face->Center;
            readout.To = face->Center;
            readout.Text = "carve needs a flat rectangular face";
            readout.Viewport = viewport.Id;
        }
    }
    else
    {
        ctx.Overlay.Readout.Clear();
    }
    return InputConsumed::Yes;
}

void FaceCarveTool::OnHoverEnd(ToolContext& ctx)
{
    // The dispatcher fires HoverEnd on every pointer move while ANY interaction
    // is active: touch only the hover glow, and only when neither dragging nor
    // holding a pending carve (a revert here would destroy the live preview).
    if (Phase != FaceCarvePhase::Idle)
        return;
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.Readout.Clear();
}

InputConsumed FaceCarveTool::OnClick(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    // A click is not a rect; consuming it keeps stray clicks from re-selecting
    // while the tool is active, and a pending carve survives.
    return InputConsumed::Yes;
}

bool FaceCarveTool::CaptureFace(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (Phase != FaceCarvePhase::Idle)
        RevertAll(ctx);

    const SelectableRef picked = ctx.Picking.Pick(viewport, pos, ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
        return false;

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(picked.Entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return false;
    const std::optional<BrushOps::BrushRectFaceFrame> frame =
        BrushOps::RectFaceFrame(*resolved->Mesh, picked.ElementId);
    if (!frame.has_value())
        return false;

    TargetEntity = picked.Entity;
    Original = *resolved->Mesh;
    FaceIndex = picked.ElementId;
    Frame = *frame;
    TargetTransform = resolved->Transform;
    DragViewport = viewport.Id;

    // The snap plane in world space: the face's frame under the entity
    // transform, on the shared grid spacing. With non-uniform scale the world
    // axes lose orthogonality and the snap lattice is approximate; the snapped
    // point converts exactly back to local coordinates either way.
    DragPlane.Origin = TargetTransform.TransformPoint(Frame.Origin);
    DragPlane.AxisU = TargetTransform.TransformVector(Frame.AxisU).Normalized();
    DragPlane.AxisV = TargetTransform.TransformVector(Frame.AxisV).Normalized();
    DragPlane.Spacing = ctx.Grid.Spacing;
    DragPlane.SnapEnabled = ctx.Grid.SnapEnabled;

    Phase = FaceCarvePhase::Dragging;
    PreviewValid = false;
    // ctx is the workspace-owned ToolContext every tool entry point receives,
    // so it outlives this scope; Commit and RevertAll close it before the tool
    // returns to Idle.
    ctx.Commands.OpenPendingEdit([this, &ctx] { RevertAll(ctx); });
    return true;
}

bool FaceCarveTool::BeginPendingAdjust(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (Phase != FaceCarvePhase::Pending || !TargetEntity.IsValid())
        return false;

    const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
    if (!uv.has_value())
        return false;

    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };
    const Vec2d corners[4] = {
        { rectMin.X, rectMin.Y },
        { rectMax.X, rectMin.Y },
        { rectMax.X, rectMax.Y },
        { rectMin.X, rectMax.Y },
    };

    const auto worldAt = [&](Vec2d p)
    {
        return TargetTransform.TransformPoint(Frame.Origin + Frame.AxisU * p.X + Frame.AxisV * p.Y);
    };

    const ViewportProjection projection(viewport);
    float bestPixels = kAdjustHandleHitPixels;
    int best = -1;
    for (int i = 0; i < 4; ++i)
    {
        const std::optional<ProjectedPoint> projected = projection.WorldToPixel(worldAt(corners[i]));
        if (!projected.has_value())
            continue;
        const float pixels = ScreenDistance(pos, projected->Pixel);
        if (pixels <= bestPixels)
        {
            bestPixels = pixels;
            best = i;
        }
    }
    if (best < 0)
        return false;

    AnchorUv = corners[(best + 2) % 4];
    LastUv = *uv;
    Phase = FaceCarvePhase::Dragging;
    DragViewport = viewport.Id;
    ctx.Overlay.HoverBody = TargetEntity;
    return true;
}

void FaceCarveTool::UpdateRectPreview(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax)
{
    // Undo can revert the carve mid-drag; the live interaction keeps forwarding
    // moves, which must not resurrect a preview from stale capture state.
    if (Phase == FaceCarvePhase::Idle)
        return;

    BrushMesh carved = Pierce
        ? BrushOps::CarveFaceRectThrough(Original, FaceIndex, rectMin, rectMax)
        : BrushOps::CarveFaceRect(Original, FaceIndex, rectMin, rectMax);
    // The ops return the input untouched on refusal, so any count change means
    // success; checked before repair, which can compact a flush pierce back to
    // the original counts (a slab removal shrinks the brush).
    if ((carved.Faces.size() != Original.Faces.size()
         || carved.Vertices.size() != Original.Vertices.size())
        && BrushValidateAndRepair(carved).Ok)
    {
        ctx.Sink.PreviewMesh(TargetEntity, carved);
        Pending = std::move(carved);
        PreviewValid = true;
    }
    else
    {
        ctx.Sink.PreviewMesh(TargetEntity, Original);
        PreviewValid = false;
    }
}

void FaceCarveTool::UpdateLoopPreview(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax)
{
    if (Phase == FaceCarvePhase::Idle)
        return;

    BrushMesh cut = Pierce
        ? BrushOps::InsertFaceLoopBoundsThrough(Original, FaceIndex, rectMin, rectMax)
        : BrushOps::InsertFaceLoopBounds(Original, FaceIndex, rectMin, rectMax);
    if ((cut.Faces.size() != Original.Faces.size()
         || cut.Vertices.size() != Original.Vertices.size())
        && BrushValidateAndRepair(cut).Ok)
    {
        ctx.Sink.PreviewMesh(TargetEntity, cut);
        Pending = std::move(cut);
        PreviewValid = true;
    }
    else
    {
        ctx.Sink.PreviewMesh(TargetEntity, Original);
        PreviewValid = false;
    }
}

std::unique_ptr<IInteraction> FaceCarveTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                                       const PointerEvent& pressPointer)
{
    if (BeginPendingAdjust(ctx, viewport, pressPointer.Position))
        return std::make_unique<CarveDragInteraction>(*this);

    if (!CaptureFace(ctx, viewport, pressPointer.Position))
        return nullptr;

    const std::optional<Vec2d> anchor = CursorUv(ctx, viewport, pressPointer.Position);
    if (!anchor.has_value())
    {
        RevertAll(ctx);
        return nullptr;
    }
    AnchorUv = *anchor;
    LastUv = *anchor;
    ctx.Overlay.HoverBody = TargetEntity;
    return std::make_unique<CarveDragInteraction>(*this);
}

void FaceCarveTool::UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    if (Phase == FaceCarvePhase::Idle)
        return;

    const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
    if (uv.has_value())
        LastUv = *uv;

    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };

    // Always edit from the captured Original: the live mesh is the preview.
    if (Mode == FaceCarveMode::EdgeLoop)
        UpdateLoopPreview(ctx, rectMin, rectMax);
    else
        UpdateRectPreview(ctx, rectMin, rectMax);
    WriteReadout(ctx, rectMin, rectMax, /*pending*/ false);
}

void FaceCarveTool::EndDrag(ToolContext& ctx)
{
    if (Phase != FaceCarvePhase::Dragging)
        return;
    if (!PreviewValid)
    {
        RevertAll(ctx);
        return;
    }
    Phase = FaceCarvePhase::Pending;
    // Release does NOT commit: the preview persists until Enter/Apply.
    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };
    WriteReadout(ctx, rectMin, rectMax, /*pending*/ true);
}

InputConsumed FaceCarveTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (Phase == FaceCarvePhase::Dragging)
        return InputConsumed::No;

    if (event.Key == SDLK_RETURN || event.Key == SDLK_KP_ENTER)
    {
        if (CanCommit())
        {
            Commit(ctx);
            return InputConsumed::Yes;
        }
        return InputConsumed::No;
    }
    if (event.Key == SDLK_ESCAPE)
    {
        if (Phase == FaceCarvePhase::Pending)
        {
            RevertAll(ctx);
            return InputConsumed::Yes;
        }
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void FaceCarveTool::OnDeactivate(ToolContext& ctx)
{
    RevertAll(ctx);
}

void FaceCarveTool::OnCancel(ToolContext& ctx)
{
    RevertAll(ctx);
}

void FaceCarveTool::Commit(ToolContext& ctx)
{
    if (!CanCommit() || !TargetEntity.IsValid())
        return;

    // Clear state into locals FIRST: CommitMesh and the tool hand-off re-enter
    // OnDeactivate -> RevertAll, which must then no-op.
    const EntityId entity = TargetEntity;
    const FaceCarveMode mode = Mode;
    const bool pierce = Pierce;
    BrushMesh before = std::move(Original);
    BrushMesh after = std::move(Pending);
    ctx.Commands.ClosePendingEdit();
    Phase = FaceCarvePhase::Idle;
    PreviewValid = false;
    TargetEntity = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.PointHandles.clear();

    if (!ctx.Sink.ResolveMesh(entity).has_value())
        return; // the entity died while pending; nothing to commit

    std::vector<SelectableRef> refs;
    MeshElementKind selectedKind = MeshElementKind::Face;
    if (mode == FaceCarveMode::EdgeLoop || pierce)
    {
        refs = NewEdgeRefs(ctx.Scene, entity, before, after);
        selectedKind = MeshElementKind::Edge;
    }
    else
    {
        const std::uint32_t centerFace = static_cast<std::uint32_t>(after.Faces.size() - 1);
        refs.push_back(SelectableRef::FaceSelection(ctx.Scene.GetRegistry().Id, entity, centerFace));
    }

    ctx.Sink.CommitMesh(entity, std::move(before), std::move(after));

    ctx.Sink.SelectElements(refs);
    ctx.MeshEdit.SetElementKind(selectedKind);
    if (ctx.ActivateTool)
        ctx.ActivateTool("select");
}

void FaceCarveTool::RevertAll(ToolContext& ctx)
{
    if (Phase != FaceCarvePhase::Idle)
    {
        ctx.Commands.ClosePendingEdit();
        if (TargetEntity.IsValid() && ctx.Sink.ResolveMesh(TargetEntity).has_value())
            ctx.Sink.PreviewMesh(TargetEntity, Original);
    }
    Phase = FaceCarvePhase::Idle;
    PreviewValid = false;
    TargetEntity = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.PointHandles.clear();
}

std::optional<Vec2d> FaceCarveTool::CursorUv(ToolContext& ctx, const EditorViewport& viewport,
                                             ImVec2 pos) const
{
    const std::optional<Vec3d> world = ctx.Picking.ProjectPointToPlane(viewport, pos, DragPlane);
    if (!world.has_value())
        return std::nullopt;

    // World -> local through the inverse transform (exact under non-uniform
    // scale), then frame coordinates, clamped onto the face.
    const Vec3d local = InverseTransformPoint(TargetTransform, *world);
    const Vec3d rel = local - Frame.Origin;
    return Vec2d{
        std::clamp(rel.Dot(Frame.AxisU), 0.0f, Frame.Width),
        std::clamp(rel.Dot(Frame.AxisV), 0.0f, Frame.Height),
    };
}

void FaceCarveTool::WriteReadout(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax, bool pending) const
{
    const auto worldAt = [&](Vec2d uv)
    {
        return TargetTransform.TransformPoint(
            Frame.Origin + Frame.AxisU * uv.X + Frame.AxisV * uv.Y);
    };
    const Vec3d a = worldAt(rectMin);
    const Vec3d b = worldAt(rectMax);
    const Vec3d alongU = worldAt({ rectMax.X, rectMin.Y });

    DragReadout& readout = ctx.Overlay.Readout;
    readout.From = a;
    readout.To = b;
    // World extents measured between transformed corners, so scale reads truthfully.
    readout.Text = FormatUnits((alongU - a).Magnitude()) + " x " + FormatUnits((b - alongU).Magnitude())
                 + (pending ? "  Enter to apply" : "");
    readout.Viewport = DragViewport;
    WriteRectHandles(ctx, rectMin, rectMax);
}

void FaceCarveTool::WriteRectHandles(ToolContext& ctx, Vec2d rectMin, Vec2d rectMax) const
{
    ctx.Overlay.PointHandles.clear();
    if (!PreviewValid || !DragViewport.IsValid())
        return;

    const Vec2d corners[4] = {
        { rectMin.X, rectMin.Y },
        { rectMax.X, rectMin.Y },
        { rectMax.X, rectMax.Y },
        { rectMin.X, rectMax.Y },
    };

    for (Vec2d uv : corners)
    {
        PointHandleRequest handle;
        handle.World = TargetTransform.TransformPoint(
            Frame.Origin + Frame.AxisU * uv.X + Frame.AxisV * uv.Y);
        handle.Fill = EditorTheme::Handle;
        handle.Border = EditorTheme::Readout;
        handle.SizePixels = EditorTheme::HandlePixels;
        ctx.Overlay.PointHandles.push_back(handle);
    }
}
