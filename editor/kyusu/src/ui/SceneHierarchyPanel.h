#pragma once

#include "ui/IEditorPanel.h"

class CommandStack;
class WorldDocument;
class SelectionService;

class SceneHierarchyPanel : public IEditorPanel
{
public:
    SceneHierarchyPanel(WorldDocument& world,
                        SelectionService& selection, CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
};
