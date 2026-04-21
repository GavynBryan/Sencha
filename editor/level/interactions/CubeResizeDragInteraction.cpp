#include "CubeResizeDragInteraction.h"

#include "../../commands/CommandStack.h"
#include "../../level/LevelCommands.h"
#include "../../level/LevelDocument.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"

#include <algorithm>
#include <cmath>
#include <memory>

CubeResizeDragInteraction::CubeResizeDragInteraction(EntityId entity,
                                                      int faceIndex,
                                                      const Transform3f& initialTransform,
                                                      Vec3d initialHalfExtents,
                                                      LevelScene& scene,
                                                      LevelDocument& document)
    : Entity(entity)
    , FaceIndex(faceIndex)
    , InitialTransform(initialTransform)
    , InitialHalfExtents(initialHalfExtents)
    , CurrentPosition(initialTransform.Position)
    , CurrentHalfExtents(initialHalfExtents)
    , Scene(scene)
    , Document(document)
{
}

void CubeResizeDragInteraction::OnPointerMove(ToolContext& ctx,
                                               EditorViewport& viewport,
                                               ImVec2 pos,
                                               ImVec2 /*delta*/)
{
    const std::optional<Vec3d> snapped = ctx.Picking.ProjectPointToGrid(viewport, pos);
    if (!snapped.has_value())
        return;

    const int axis = FaceIndex / 2;
    const GridPlane grid = viewport.GetGrid();
    const Vec3d gridNormal = grid.AxisU.Cross(grid.AxisV).Normalized();
    Vec3d dragAxis = {};
    dragAxis[axis] = 1.0f;
    if (std::abs(gridNormal.Dot(dragAxis)) > 0.99f)
        return;

    const float sign = (FaceIndex % 2 == 0) ? 1.0f : -1.0f;
    const float fixedFacePos = InitialTransform.Position[axis] - sign * InitialHalfExtents[axis];
    const float newFacePos = (*snapped)[axis];
    const float minHalf = viewport.GetGrid().Spacing * 0.5f;
    const float half = std::max(std::abs(newFacePos - fixedFacePos) * 0.5f, minHalf);

    Vec3d newPos = InitialTransform.Position;
    newPos[axis] = (newFacePos + fixedFacePos) * 0.5f;

    Vec3d newHE = InitialHalfExtents;
    newHE[axis] = half;

    CurrentPosition = newPos;
    CurrentHalfExtents = newHE;
    ApplyResize(Scene, newPos, newHE);
}

void CubeResizeDragInteraction::OnPointerUp(ToolContext& ctx,
                                             EditorViewport& /*viewport*/,
                                             ImVec2 /*pos*/)
{
    ctx.Commands.Execute(std::make_unique<EditCubeCommand>(
        Entity,
        InitialTransform.Position,
        CurrentPosition,
        InitialHalfExtents,
        CurrentHalfExtents,
        Scene,
        Document));
}

void CubeResizeDragInteraction::OnCancel(ToolContext& /*ctx*/)
{
    ApplyResize(Scene, InitialTransform.Position, InitialHalfExtents);
}

void CubeResizeDragInteraction::ApplyResize(LevelScene& scene, Vec3d newPosition, Vec3d newHalfExtents)
{
    if (const Transform3f* t = scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = newPosition;
        scene.SetTransform(Entity, updated);
    }
    scene.SetCubeHalfExtents(Entity, newHalfExtents);
}
