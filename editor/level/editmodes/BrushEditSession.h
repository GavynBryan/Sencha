#pragma once

#include "../../editmodes/IEditSession.h"
#include "../../editmodes/handles/IHandle.h"
#include "../../selection/SelectableRef.h"

#include <memory>
#include <vector>

class LevelDocument;
class LevelScene;

class BrushEditSession : public IEditSession
{
public:
    BrushEditSession(SelectableRef selection, LevelScene& scene, LevelDocument& document);

    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) override;

private:
    void BuildHandles();

    SelectableRef Selection;
    LevelScene& Scene;
    LevelDocument& Document;
    std::vector<std::unique_ptr<IHandle>> FaceHandles;
    std::unique_ptr<IHandle> BodyHandle;
};
