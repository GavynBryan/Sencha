#pragma once

#include "IEditorPanel.h"

class CommandStack;
class MeshEditService;
class SelectionService;
struct IMeshEditTarget;

// Hammer-style mesh-element editing surface. Hosts the Object/Vertex/Edge/Face
// mode toolbar (which drives what clicking selects) and the per-mode edit verbs
// (extrude/delete for faces). All edits go through MeshEditService against the
// injected edit target; the panel never touches BrushOps or the scene directly.
// (docs/plans/sencha-level-editor mesh-edit subsystem.)
class MeshEditPanel : public IEditorPanel
{
public:
    MeshEditPanel(IMeshEditTarget& target,
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

    IMeshEditTarget& Target;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;

    float ExtrudeDistance = 1.0f;
    bool Visible = true;
};
