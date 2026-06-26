#pragma once

#include "IEditorPanel.h"

#include <functional>

class CommandStack;
class ManipulatorSession;
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
                  CommandStack& commands,
                  ManipulatorSession& manipulators,
                  std::function<void()> setOriginToPivot);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }

private:
    void DrawGizmoToolbar();
    void DrawModeToolbar();
    void DrawObjectVerbs();
    void DrawFaceVerbs();
    void DrawEdgeVerbs();

    IMeshEditTarget& Target;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;
    ManipulatorSession& Manipulators;
    std::function<void()> SetOriginToPivot;

    float ExtrudeDistance = 1.0f;
    float CutPosition = 0.5f; // edge-cut authored position (0..1 along the edge)
    bool CutLoop = true;      // edge-cut: full loop vs single edge
};
