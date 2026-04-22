#pragma once

#include "../../editmodes/handles/IHandle.h"
#include "../../selection/SelectableRef.h"

class LevelScene;

class LevelDocument;

class BrushFaceHandle : public IHandle
{
public:
    explicit BrushFaceHandle(SelectableRef face, LevelScene& scene, LevelDocument& document);

    HandleHit HitTest(const EditorViewport& viewport, ImVec2 screenPos) const override;
    std::unique_ptr<IInteraction> BeginDrag(ToolContext& ctx,
                                            const EditorViewport& viewport,
                                            ImVec2 screenPos) const override;

private:
    SelectableRef Face;
    LevelScene& Scene;
    LevelDocument& Document;
};
