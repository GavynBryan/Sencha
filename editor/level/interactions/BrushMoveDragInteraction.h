#pragma once

#include "../../interaction/IInteraction.h"
#include "../../level/BrushGeometry.h"
#include "../../level/LevelScene.h"

#include <world/entity/EntityId.h>

class LevelDocument;

class BrushMoveDragInteraction : public IInteraction
{
public:
    BrushMoveDragInteraction(EntityId entity,
                             BrushState initialState,
                             Vec3d gridAnchor,
                             LevelScene& scene,
                             LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnCancel(ToolContext& ctx) override;

private:
    EntityId Entity;
    BrushState InitialState;
    Vec3d GridAnchor;
    BrushState CurrentState;
    LevelScene& Scene;
    LevelDocument& Document;
};
