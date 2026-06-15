#include "BrushMoveDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../commands/MoveEntityCommand.h"
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
    // Move only the transform — never rebuild the mesh (that would clobber any
    // extrude/clip/delete edits).
    Scene.SetTransform(Entity, CurrentState.Transform);
}

void BrushMoveDragInteraction::OnPointerUp(ToolContext& ctx,
                                           EditorViewport& /*viewport*/,
                                           ImVec2 /*pos*/)
{
    ctx.Commands.Execute(std::make_unique<MoveEntityCommand>(
        Entity,
        InitialState.Transform,
        CurrentState.Transform,
        Scene,
        Document));
}

void BrushMoveDragInteraction::OnCancel(ToolContext& /*ctx*/)
{
    // Restore the transform only; the mesh was never touched.
    Scene.SetTransform(Entity, InitialState.Transform);
}
