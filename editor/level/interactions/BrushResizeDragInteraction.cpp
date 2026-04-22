#include "BrushResizeDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../level/LevelCommands.h"
#include "../../level/LevelDocument.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <algorithm>
#include <cmath>
#include <memory>

BrushResizeDragInteraction::BrushResizeDragInteraction(SelectableRef face,
                                                       BrushState initialState,
                                                       LevelScene& scene,
                                                       LevelDocument& document)
    : Face(face)
    , InitialState(initialState)
    , CurrentState(initialState)
    , Scene(scene)
    , Document(document)
{
}

void BrushResizeDragInteraction::OnPointerMove(ToolContext& ctx,
                                               EditorViewport& viewport,
                                               ImVec2 pos,
                                               ImVec2 /*delta*/)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (!snapped.has_value())
        return;

    const GridPlane grid = viewport.GetGrid();
    const Vec3d gridNormal = grid.AxisU.Cross(grid.AxisV).Normalized();
    const int axis = static_cast<int>(Face.ElementId / 2);
    Vec3d dragAxis = {};
    dragAxis[axis] = 1.0f;
    if (std::abs(gridNormal.Dot(dragAxis)) > 0.99f)
        return;

    const float newFacePos = (*snapped)[axis];
    const float minHalf = viewport.GetGrid().Spacing * 0.5f;
    CurrentState = BrushGeometry::ResizeFace(InitialState, static_cast<int>(Face.ElementId), newFacePos, minHalf);
    BrushGeometry::ApplyState(Scene, Face.Entity, CurrentState);
}

void BrushResizeDragInteraction::OnPointerUp(ToolContext& ctx,
                                             EditorViewport& /*viewport*/,
                                             ImVec2 /*pos*/)
{
    ctx.Commands.Execute(std::make_unique<EditBrushCommand>(
        Face.Entity,
        InitialState.Transform.Position,
        CurrentState.Transform.Position,
        InitialState.HalfExtents,
        CurrentState.HalfExtents,
        Scene,
        Document));
}

void BrushResizeDragInteraction::OnCancel(ToolContext& /*ctx*/)
{
    BrushGeometry::ApplyState(Scene, Face.Entity, InitialState);
}
