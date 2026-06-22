#pragma once

#include "IEditorPanel.h"

class CommandStack;
class LevelScene;
class LevelDocument;
class SelectionService;

class SceneHierarchyPanel : public IEditorPanel
{
public:
    SceneHierarchyPanel(LevelScene& scene, LevelDocument& document,
                        SelectionService& selection, CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    LevelScene& Scene;
    LevelDocument& Document;
    SelectionService& Selection;
    CommandStack& Commands;
};
