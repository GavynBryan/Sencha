#include "FaceCarveTool.h"

#include "fonts/IconsFontAwesome6.h"

#include "document/EditorScene.h"
#include "brush/BrushTransform.h"
#include "brush/BrushValidation.h"
#include "meshedit/ManipulationSink.h"
#include "meshedit/MeshElements.h"
#include "meshedit/MeshEditService.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "selection/SelectableRef.h"
#include "tools/ToolContext.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace
{
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

InputConsumed FaceCarveTool::OnHover(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    // A pending carve keeps its own glow and readout; hover changes nothing.
    if (PendingValid || Dragging)
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
    if (Dragging || PendingValid)
        return;
    ctx.Overlay.HoverBody = {};
    ctx.Overlay.Readout.Clear();
}

InputConsumed FaceCarveTool::OnClick(ToolContext&, EditorViewport&, const PointerEvent&)
{
    // A click is not a rect; consuming it keeps stray clicks from re-selecting
    // while the tool is active, and a pending carve survives.
    return InputConsumed::Yes;
}

std::unique_ptr<IInteraction> FaceCarveTool::BeginDrag(ToolContext& ctx, EditorViewport& viewport,
                                                       const PointerEvent& pressPointer)
{
    const SelectableRef picked = ctx.Picking.Pick(viewport, pressPointer.Position, ctx.Scene,
        BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
        return nullptr; // an existing pending carve is kept; a stray drag must not destroy it

    // Any prior pending preview (possibly on a DIFFERENT entity) must revert
    // BEFORE re-resolving: ResolveMesh returns the live (previewed) mesh, and
    // the new snapshot must be the real geometry.
    RevertAll(ctx);

    const std::optional<MeshEditTargetMesh> resolved = ctx.Sink.ResolveMesh(picked.Entity);
    if (!resolved.has_value() || resolved->Mesh == nullptr)
        return nullptr;
    const std::optional<BrushOps::BrushRectFaceFrame> frame =
        BrushOps::RectFaceFrame(*resolved->Mesh, picked.ElementId);
    if (!frame.has_value())
        return nullptr;

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

    const std::optional<Vec2d> anchor = CursorUv(ctx, viewport, pressPointer.Position);
    if (!anchor.has_value())
    {
        TargetEntity = {};
        return nullptr;
    }
    AnchorUv = *anchor;
    LastUv = *anchor;
    Dragging = true;
    ctx.Overlay.HoverBody = TargetEntity;
    return std::make_unique<CarveDragInteraction>(*this);
}

void FaceCarveTool::UpdateDrag(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos)
{
    const std::optional<Vec2d> uv = CursorUv(ctx, viewport, pos);
    if (uv.has_value())
        LastUv = *uv;

    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };

    // Always carve from the captured Original: the live mesh is the preview.
    BrushMesh carved = BrushOps::CarveFaceRect(Original, FaceIndex, rectMin, rectMax);
    if (carved.Faces.size() > Original.Faces.size() && BrushValidateAndRepair(carved).Ok)
    {
        ctx.Sink.PreviewMesh(TargetEntity, carved);
        Pending = std::move(carved);
        PendingValid = true;
    }
    else
    {
        ctx.Sink.PreviewMesh(TargetEntity, Original);
        PendingValid = false;
    }
    WriteReadout(ctx, rectMin, rectMax, /*pending*/ false);
}

void FaceCarveTool::EndDrag(ToolContext& ctx)
{
    Dragging = false;
    if (!PendingValid)
    {
        RevertAll(ctx);
        return;
    }
    // Release does NOT commit: the preview persists until Enter/Apply.
    const Vec2d rectMin{ std::min(AnchorUv.X, LastUv.X), std::min(AnchorUv.Y, LastUv.Y) };
    const Vec2d rectMax{ std::max(AnchorUv.X, LastUv.X), std::max(AnchorUv.Y, LastUv.Y) };
    WriteReadout(ctx, rectMin, rectMax, /*pending*/ true);
}

InputConsumed FaceCarveTool::OnKeyDown(ToolContext& ctx, const KeyDownEvent& event)
{
    if (!PendingValid || Dragging)
        return InputConsumed::No; // Escape without a pending carve climbs the editing context

    if (event.Key == SDLK_RETURN || event.Key == SDLK_KP_ENTER)
    {
        Commit(ctx);
        return InputConsumed::Yes;
    }
    if (event.Key == SDLK_ESCAPE)
    {
        RevertAll(ctx);
        return InputConsumed::Yes;
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
    if (!PendingValid || !TargetEntity.IsValid())
        return;

    // Clear state into locals FIRST: CommitMesh and the tool hand-off re-enter
    // OnDeactivate -> RevertAll, which must then no-op.
    const EntityId entity = TargetEntity;
    BrushMesh before = std::move(Original);
    BrushMesh after = std::move(Pending);
    PendingValid = false;
    Dragging = false;
    TargetEntity = {};
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};

    if (!ctx.Sink.ResolveMesh(entity).has_value())
        return; // the entity died while pending; nothing to commit

    const std::uint32_t centerFace = static_cast<std::uint32_t>(after.Faces.size() - 1);
    ctx.Sink.CommitMesh(entity, std::move(before), std::move(after));

    // Hand off ready-to-extrude: the kept center face selected in Face mode
    // under the Select tool (the kernel appends it last).
    const std::array<SelectableRef, 1> refs = {
        SelectableRef::FaceSelection(ctx.Scene.GetRegistry().Id, entity, centerFace)
    };
    ctx.Sink.SelectElements(refs);
    ctx.MeshEdit.SetElementKind(MeshElementKind::Face);
    if (ctx.ActivateTool)
        ctx.ActivateTool("select");
}

void FaceCarveTool::RevertAll(ToolContext& ctx)
{
    if (TargetEntity.IsValid() && ctx.Sink.ResolveMesh(TargetEntity).has_value())
        ctx.Sink.PreviewMesh(TargetEntity, Original);
    TargetEntity = {};
    PendingValid = false;
    Dragging = false;
    ctx.Overlay.Readout.Clear();
    ctx.Overlay.HoverBody = {};
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
}
