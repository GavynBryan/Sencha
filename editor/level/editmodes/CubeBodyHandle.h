#pragma once

#include "../../editmodes/handles/IHandle.h"
#include "../../level/LevelScene.h"

#include <world/entity/EntityId.h>

class LevelDocument;

class CubeBodyHandle : public IHandle
{
public:
    CubeBodyHandle(EntityId entity, LevelScene& scene, LevelDocument& document);

    HandleHit HitTest(const EditorViewport& viewport, ImVec2 screenPos) const override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx,
                                            const EditorViewport& viewport,
                                            ImVec2 screenPos) const override;

private:
    EntityId Entity;
    LevelScene& Scene;
    LevelDocument& Document;
};
