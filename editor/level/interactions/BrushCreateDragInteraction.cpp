#include "BrushCreateDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../level/LevelCommands.h"
#include "../../level/LevelDocument.h"
#include "../../level/LevelScene.h"
#include "../../render/PreviewBuffer.h"
#include "../../selection/SelectCommand.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <world/registry/EntityRef.h>

#include <cmath>
#include <memory>

BrushCreateDragInteraction::BrushCreateDragInteraction(Vec3d anchorGrid,
                                                       LevelScene& scene,
                                                       LevelDocument& document)
    : AnchorGrid(anchorGrid)
    , LastCenter(anchorGrid)
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

void BrushCreateDragInteraction::UpdatePreview(ToolContext& ctx,
                                               Vec3d snapped,
                                               const EditorViewport& viewport)
{
    const GridPlane grid = viewport.GetGrid();
    const int uIdx = AxisIndex(grid.AxisU);
    const int vIdx = AxisIndex(grid.AxisV);
    const float minHalf = grid.Spacing * 0.5f;

    Vec3d halfExtents(0.5f, 0.5f, 0.5f);
    halfExtents[uIdx] = std::max(std::abs(snapped[uIdx] - AnchorGrid[uIdx]) * 0.5f, minHalf);
    halfExtents[vIdx] = std::max(std::abs(snapped[vIdx] - AnchorGrid[vIdx]) * 0.5f, minHalf);

    Vec3d center = AnchorGrid;
    center[uIdx] = (AnchorGrid[uIdx] + snapped[uIdx]) * 0.5f;
    center[vIdx] = (AnchorGrid[vIdx] + snapped[vIdx]) * 0.5f;

    const float dragU = std::abs(snapped[uIdx] - AnchorGrid[uIdx]);
    const float dragV = std::abs(snapped[vIdx] - AnchorGrid[vIdx]);
    HasValidSize = (dragU >= grid.Spacing || dragV >= grid.Spacing);

    LastCenter = center;
    LastHalfExtents = halfExtents;
    ctx.Preview.SetBox(center, halfExtents);
}

void BrushCreateDragInteraction::OnPointerMove(ToolContext& ctx,
                                               EditorViewport& viewport,
                                               ImVec2 pos,
                                               ImVec2 /*delta*/)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (!snapped.has_value())
        return;

    UpdatePreview(ctx, *snapped, viewport);
}

void BrushCreateDragInteraction::OnPointerUp(ToolContext& ctx,
                                             EditorViewport& viewport,
                                             ImVec2 pos)
{
    ctx.Preview.Clear();

    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (snapped.has_value())
        UpdatePreview(ctx, *snapped, viewport);

    ctx.Preview.Clear();

    if (!HasValidSize)
        return;

    auto cmd = std::make_unique<CreateBrushCommand>(LastCenter, LastHalfExtents, Scene, Document);
    CreateBrushCommand* rawCmd = cmd.get();
    ctx.Commands.Execute(std::move(cmd));

    const EntityId created = rawCmd->GetCreatedEntity();
    if (created.IsValid())
    {
        const SelectableRef ref{
            .Registry = Scene.GetRegistry().Id,
            .Entity = created,
        };
        ctx.Commands.Execute(std::make_unique<SelectCommand>(ctx.Selection, ref));
    }
}

void BrushCreateDragInteraction::OnCancel(ToolContext& ctx)
{
    ctx.Preview.Clear();
    HasValidSize = false;
}
