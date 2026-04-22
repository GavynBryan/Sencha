#include "BrushMoveDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../level/LevelCommands.h"
#include "../../level/LevelDocument.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <memory>

BrushMoveDragInteraction::BrushMoveDragInteraction(EntityId entity,
                                                   BrushState initialState,
                                                   Vec3d gridAnchor,
                                                   LevelScene& scene,
                                                   LevelDocument& document)
    : Entity(entity)
    , InitialState(initialState)
    , GridAnchor(gridAnchor)
    , CurrentState(initialState)
    , Scene(scene)
    , Document(document)
{
}

void BrushMoveDragInteraction::OnPointerMove(ToolContext& ctx,
                                             EditorViewport& viewport,
                                             ImVec2 pos,
                                             ImVec2 /*delta*/)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (!snapped.has_value())
        return;

    const Vec3d moveDelta = *snapped - GridAnchor;
    CurrentState = BrushGeometry::Translate(InitialState, moveDelta);
    BrushGeometry::ApplyState(Scene, Entity, CurrentState);
}

void BrushMoveDragInteraction::OnPointerUp(ToolContext& ctx,
                                           EditorViewport& /*viewport*/,
                                           ImVec2 /*pos*/)
{
    ctx.Commands.Execute(std::make_unique<EditBrushCommand>(
        Entity,
        InitialState.Transform.Position,
        CurrentState.Transform.Position,
        InitialState.HalfExtents,
        CurrentState.HalfExtents,
        Scene,
        Document));
}

void BrushMoveDragInteraction::OnCancel(ToolContext& /*ctx*/)
{
    BrushGeometry::ApplyState(Scene, Entity, InitialState);
}
