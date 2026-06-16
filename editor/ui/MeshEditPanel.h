#pragma once

#include "IEditorPanel.h"

class CommandStack;
class LevelDocument;
class LevelScene;
class MeshEditService;
class SelectionService;

// Hammer-style mesh-element editing surface. Hosts the Object/Vertex/Edge/Face
// mode toolbar (which drives what clicking selects) and the per-mode edit verbs
// (extrude/delete for faces). All edits go through MeshEditService; the panel
// never touches BrushOps. (docs/plans/sencha-level-editor mesh-edit subsystem.)
class MeshEditPanel : public IEditorPanel
{
public:
    MeshEditPanel(LevelScene& scene,
                  LevelDocument& document,
                  SelectionService& selection,
                  MeshEditService& meshEdit,
                  CommandStack& commands);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    void DrawModeToolbar();
    void DrawFaceVerbs();
    void DrawEdgeVerbs();

    LevelScene& Scene;
    LevelDocument& Document;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;

    float ExtrudeDistance = 1.0f;
    bool Visible = true;
};
