#include "CubeMoveDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../level/LevelCommands.h"
#include "../../level/LevelDocument.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <memory>

CubeMoveDragInteraction::CubeMoveDragInteraction(EntityId entity,
                                                  const Transform3f& initialTransform,
                                                  Vec3d initialHalfExtents,
                                                  Vec3d gridAnchor,
                                                  LevelScene& scene,
                                                  LevelDocument& document)
    : Entity(entity)
    , InitialTransform(initialTransform)
    , InitialHalfExtents(initialHalfExtents)
    , GridAnchor(gridAnchor)
    , CurrentPosition(initialTransform.Position)
    , Scene(scene)
    , Document(document)
{
}

void CubeMoveDragInteraction::OnPointerMove(ToolContext& ctx,
                                             EditorViewport& viewport,
                                             ImVec2 pos,
                                             ImVec2 /*delta*/)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (!snapped.has_value())
        return;

    const Vec3d moveDelta = *snapped - GridAnchor;
    CurrentPosition = InitialTransform.Position + moveDelta;

    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = CurrentPosition;
        Scene.SetTransform(Entity, updated);
    }
}

void CubeMoveDragInteraction::OnPointerUp(ToolContext& ctx,
                                           EditorViewport& /*viewport*/,
                                           ImVec2 /*pos*/)
{
    ctx.Commands.Execute(std::make_unique<EditCubeCommand>(
        Entity,
        InitialTransform.Position,
        CurrentPosition,
        InitialHalfExtents,
        InitialHalfExtents,
        Scene,
        Document));
}

void CubeMoveDragInteraction::OnCancel(ToolContext& /*ctx*/)
{
    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f restored = *t;
        restored.Position = InitialTransform.Position;
        Scene.SetTransform(Entity, restored);
    }
}
