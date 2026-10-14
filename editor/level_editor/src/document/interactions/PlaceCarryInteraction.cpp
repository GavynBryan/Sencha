#include "PlaceCarryInteraction.h"

#include "document/placement/PlacementCommand.h"
#include "document/placement/PlacementResolver.h"
#include "document/placement/PlacementSession.h"
#include "tools/ToolContext.h"

#include <utility>

PlaceCarryInteraction::PlaceCarryInteraction(PlacementSubject subject, float yaw)
    : Subject(std::move(subject))
    , Yaw(yaw)
{
}

void PlaceCarryInteraction::OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer)
{
    if (const std::optional<PlacementSite> site =
            PlacementSiteAt(ctx.Picking, viewport, pointer.Position, ctx.Scene, ctx.Grid))
        ctx.Placement.Aim(Subject, *site, Yaw, ctx.Document);
    else
        ctx.Placement.Withdraw();
}

void PlaceCarryInteraction::OnPointerUp(ToolContext& ctx, EditorViewport&, const PointerEvent&)
{
    (void)PlacePending(ctx.Placement, ctx.Document, ctx.Selection, ctx.Commands);
}

void PlaceCarryInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Placement.Clear();
}

void PlaceCarryInteraction::OnPointerLeftViews(ToolContext& ctx)
{
    ctx.Placement.Withdraw();
}
