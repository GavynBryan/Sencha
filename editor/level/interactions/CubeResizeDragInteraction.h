#pragma once

#include "../../interaction/IInteraction.h"
#include "../../level/LevelScene.h"

#include <math/geometry/3d/Transform3d.h>
#include <world/entity/EntityId.h>

class LevelDocument;

class CubeResizeDragInteraction : public IInteraction
{
public:
    // FaceIndex: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    CubeResizeDragInteraction(EntityId entity,
                               int faceIndex,
                               const Transform3f& initialTransform,
                               Vec3d initialHalfExtents,
                               LevelScene& scene,
                               LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnCancel(ToolContext& ctx) override;

private:
    void ApplyResize(LevelScene& scene, Vec3d newPosition, Vec3d newHalfExtents);

    EntityId Entity;
    int FaceIndex;
    Transform3f InitialTransform;
    Vec3d InitialHalfExtents;
    Vec3d CurrentPosition;
    Vec3d CurrentHalfExtents;
    LevelScene& Scene;
    LevelDocument& Document;
};
