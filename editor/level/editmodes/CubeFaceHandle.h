#pragma once

#include "../../editmodes/handles/IHandle.h"
#include "../../level/LevelScene.h"

#include <world/entity/EntityId.h>

class LevelDocument;

class CubeFaceHandle : public IHandle
{
public:
    // FaceIndex: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    CubeFaceHandle(EntityId entity, int faceIndex, LevelScene& scene, LevelDocument& document);

    HandleHit HitTest(const EditorViewport& viewport, ImVec2 screenPos) const override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx,
                                            const EditorViewport& viewport,
                                            ImVec2 screenPos) const override;

private:
    EntityId Entity;
    int FaceIndex;
    LevelScene& Scene;
    LevelDocument& Document;
};
