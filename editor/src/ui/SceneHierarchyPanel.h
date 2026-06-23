#pragma once

#include "IEditorPanel.h"

class CommandStack;
class EditorScene;
class EditorDocument;
class SelectionService;

class SceneHierarchyPanel : public IEditorPanel
{
public:
    SceneHierarchyPanel(EditorScene& scene, EditorDocument& document,
                        SelectionService& selection, CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    EditorScene& Scene;
    EditorDocument& Document;
    SelectionService& Selection;
    CommandStack& Commands;
};
