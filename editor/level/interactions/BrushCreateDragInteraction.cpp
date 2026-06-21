#include "BrushCreateDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../commands/CreateEntityCommand.h"
#include "../../level/LevelDocument.h"
#include "../../level/LevelScene.h"
#include "../../render/PreviewBuffer.h"
#include "../../selection/commands/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <cmath>
#include <memory>
#include <utility>

BrushCreateDragInteraction::BrushCreateDragInteraction(BrushCreationPlane plane,
                                                       LevelScene& scene,
                                                       LevelDocument& document)
    : Plane(plane)
    , LastCenter(plane.Anchor)
    , LastHalfExtents(Vec3d(0.5f, 0.5f, 0.5f))
    , Scene(scene)
    , Document(document)
{
}

int BrushCreateDragInteraction::AxisIndex(Vec3d axis)
{
    if (std::abs(axis.X) > 0.5f) return 0;
    if (std::abs(axis.Y) > 0.5f) return 1;
    return 2;
}

void BrushCreateDragInteraction::UpdatePreview(ToolContext& ctx, Vec3d snapped)
{
    const int uIdx = AxisIndex(Plane.Plane.AxisU);
    const int vIdx = AxisIndex(Plane.Plane.AxisV);
    const int dIdx = Plane.DepthAxis;
    const float spacing = ctx.Grid.Spacing;
    const float minHalf = spacing * 0.5f;

    Vec3d halfExtents{};
    halfExtents[uIdx] = std::max(std::abs(snapped[uIdx] - Plane.Anchor[uIdx]) * 0.5f, minHalf);
    halfExtents[vIdx] = std::max(std::abs(snapped[vIdx] - Plane.Anchor[vIdx]) * 0.5f, minHalf);
    halfExtents[dIdx] = Plane.DepthHalf;

    Vec3d center{};
    center[uIdx] = (Plane.Anchor[uIdx] + snapped[uIdx]) * 0.5f;
    center[vIdx] = (Plane.Anchor[vIdx] + snapped[vIdx]) * 0.5f;
    center[dIdx] = Plane.DepthCenter;

    const float dragU = std::abs(snapped[uIdx] - Plane.Anchor[uIdx]);
    const float dragV = std::abs(snapped[vIdx] - Plane.Anchor[vIdx]);
    HasValidSize = (dragU >= spacing || dragV >= spacing);

    LastCenter = center;
    LastHalfExtents = halfExtents;
    ctx.Preview.SetBox(center, halfExtents);
}

void BrushCreateDragInteraction::OnPointerMove(ToolContext& ctx,
                                               EditorViewport& viewport,
                                               const PointerEvent& pointer)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToPlane(viewport, pointer.Position, Plane.Plane);
    if (!snapped.has_value())
        return;

    UpdatePreview(ctx, *snapped);
}

void BrushCreateDragInteraction::OnPointerUp(ToolContext& ctx,
                                             EditorViewport& viewport,
                                             const PointerEvent& pointer)
{
    ctx.Preview.Clear();

    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToPlane(viewport, pointer.Position, Plane.Plane);
    if (snapped.has_value())
        UpdatePreview(ctx, *snapped);

    ctx.Preview.Clear();

    if (!HasValidSize)
        return;

    auto cmd = MakeCreateBrushCommand(LastCenter, LastHalfExtents, Scene, Document);
    CreateEntityCommand* rawCmd = cmd.get();
    ctx.Commands.Execute(std::move(cmd));

    const EntityId created = rawCmd->GetCreatedEntity();
    if (created.IsValid())
    {
        const SelectableRef ref = SelectableRef::EntitySelection(Scene.GetRegistry().Id, created);
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, ref));
    }
}

void BrushCreateDragInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Preview.Clear();
    HasValidSize = false;
}
