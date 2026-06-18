#pragma once

#include "IEditorPanel.h"

class CommandStack;
class LevelScene;
class SelectionService;

class SceneHierarchyPanel : public IEditorPanel
{
public:
    SceneHierarchyPanel(LevelScene& scene, SelectionService& selection, CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    LevelScene& Scene;
    SelectionService& Selection;
    CommandStack& Commands;
};
