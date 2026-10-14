#include "PlaceTurnInteraction.h"

#include "document/placement/PlacementCommand.h"
#include "document/placement/PlacementSession.h"
#include "document/placement/PlacementTurn.h"
#include "editmodes/GizmoMath.h"
#include "overlay/EditorOverlayState.h"
#include "tools/ToolContext.h"
#include "ui/EditorUiStyle.h"
#include "viewport/EditorViewport.h"
#include "viewport/GridSettings.h"
#include "viewport/ViewportProjection.h"

#include <format>
#include <numbers>

namespace
{
// Nearer the spot than this, the direction to the pointer is too unsteady to
// face: the yaw holds.
constexpr float kHoldPixels = 12.0f;
} // namespace

PlaceTurnInteraction::PlaceTurnInteraction(float step, float& restingYaw)
    : Step(step)
    , RestingYaw(restingYaw)
{
}

void PlaceTurnInteraction::OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    const PendingPlacement* pending = ctx.Placement.Pending();
    if (pending == nullptr)
        return;
    const ViewportProjection projection(viewport);
    const std::optional<PlacementTurn::Aim> aim =
        PlacementTurn::FacingToward(pending->Site, projection.RayThroughPixel(pointer.Position),
                                    projection.WorldSizeForPixels(pending->Site.Point, EditorUi::Px(kHoldPixels)));
    if (!aim.has_value())
    {
        ctx.Placement.Turn(pending->Yaw, std::nullopt);
        ctx.Overlay.Readout.Clear();
        return;
    }
    const float step = ctx.Grid.SnapEnabled ? Step : 0.0f;
    ctx.Placement.Turn(static_cast<float>(GizmoMath::SnapAngle(aim->Yaw, step)), aim->Toward);

    DragReadout& readout = ctx.Overlay.Readout;
    readout.From = pending->Site.Point;
    readout.To = aim->Toward;
    readout.Text = std::format("{:.0f}°", pending->Yaw * 180.0f / std::numbers::pi_v<float>);
    readout.Viewport = viewport.Id;
}

void PlaceTurnInteraction::OnPointerUp(ToolContext& ctx, EditorViewport&, const PointerEvent&)
{
    ctx.Overlay.Readout.Clear();
    if (const std::optional<PendingPlacement> placed =
            PlacePending(ctx.Placement, ctx.Document, ctx.Selection, ctx.Commands))
        RestingYaw = placed->Yaw;
}

void PlaceTurnInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Overlay.Readout.Clear();
    ctx.Placement.CancelTurn();
}
