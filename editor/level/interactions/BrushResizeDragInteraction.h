#pragma once

#include "../../interaction/IInteraction.h"
#include "../../level/BrushGeometry.h"
#include "../../level/LevelScene.h"

#include <world/entity/EntityId.h>

class LevelDocument;

class BrushResizeDragInteraction : public IInteraction
{
public:
    // FaceIndex: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    BrushResizeDragInteraction(EntityId entity,
                               int faceIndex,
                               BrushState initialState,
                               LevelScene& scene,
                               LevelDocument& document);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;
    void OnCancel(ToolContext& ctx) override;

private:
    EntityId Entity;
    int FaceIndex;
    BrushState InitialState;
    BrushState CurrentState;
    LevelScene& Scene;
    LevelDocument& Document;
};
