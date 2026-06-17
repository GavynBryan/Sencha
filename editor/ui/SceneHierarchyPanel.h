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
    bool IsVisible() const override;
    void OnDraw() override;

private:
    LevelScene& Scene;
    SelectionService& Selection;
    CommandStack& Commands;
    bool Visible = true;
};
