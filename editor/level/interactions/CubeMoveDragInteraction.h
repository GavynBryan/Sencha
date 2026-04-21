#pragma once

#include "../../interaction/IInteraction.h"
#include "../../level/LevelScene.h"

#include <math/geometry/3d/Transform3d.h>
#include <world/entity/EntityId.h>

class LevelDocument;

class CubeMoveDragInteraction : public IInteraction
{
public:
    CubeMoveDragInteraction(EntityId entity,
                             const Transform3f& initialTransform,
                             Vec3d initialHalfExtents,
                             Vec3d gridAnchor,
                             LevelScene& scene,
                             LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnCancel(ToolContext& ctx) override;

private:
    EntityId Entity;
    Transform3f InitialTransform;
    Vec3d InitialHalfExtents;
    Vec3d GridAnchor;
    Vec3d CurrentPosition;
    LevelScene& Scene;
    LevelDocument& Document;
};
