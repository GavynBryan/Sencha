#pragma once

#include "ui/IEditorPanel.h"

#include <functional>

class CommandStack;
class MeshEditService;
class SelectionService;
struct IMeshEditTarget;

// The contextual mesh-edit verb panel: per-element-mode operations (extrude,
// delete, cuts, bake/revert/export for objects). Mode and gizmo switching live
// in the top toolbar; this panel only hosts the verbs. All edits go through
// MeshEditService against the injected edit target; the panel never touches
// BrushOps or the scene directly.
class MeshEditPanel : public IEditorPanel
{
public:
    // Host wiring for the object verbs that need scene/asset access (the panel
    // stays scene- and asset-agnostic). Any callback may be empty; the buttons
    // show only for applicable selections.
    struct ObjectActions
    {
        std::function<void()> Duplicate;   // independent copies (Ctrl+D)
        std::function<void()> Instance;    // copies SHARING the source mesh (Alt+D)
        std::function<void()> MakeUnique;  // break selected brushes out of their instance groups
        std::function<void()> Merge;       // join selected brushes into the primary
        std::function<void()> SeparateFaces; // split the selected faces into a new brush
        std::function<void()> Bake;        // selected brushes -> .smesh + component swap
        std::function<void()> Revert;      // selected baked entities -> brushes again
        std::function<void()> ExportGlb;   // selected brush/baked mesh -> .glb on disk
        std::function<bool()> HasBakedSelection;     // any selected entity with a dormant brush
        std::function<bool()> HasInstancedSelection; // any selected instanced brush
    };

    MeshEditPanel(IMeshEditTarget& target,
                  SelectionService& selection,
                  MeshEditService& meshEdit,
                  CommandStack& commands,
                  ObjectActions objectActions);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }

private:
    void DrawObjectVerbs();
    void DrawFaceVerbs();
    void DrawEdgeVerbs();

    IMeshEditTarget& Target;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;
    ObjectActions Objects;

    float ExtrudeDistance = 1.0f;
    float CutPosition = 0.5f; // edge-cut authored position (0..1 along the edge)
    bool CutLoop = true;      // edge-cut: full loop vs single edge
};
