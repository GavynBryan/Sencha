#pragma once

#include <functional>

class ManipulatorSession;
class ToolRegistry;
class MeshEditService;
struct GridSettings;
struct BrushCreationSettings;
struct EdgeCutSettings;

// The top icon toolbar (fixed app chrome, not a dockable panel). Backed control
// groups, each with an active-state highlight:
//   - tools: the registered ITools (Select/Brush/Camera), driving ToolRegistry;
//   - mesh element mode: Object/Vertex/Edge/Face, driving MeshEditService;
//   - transform: the gizmo (Resize/Move/Rotate/Scale), its space (grid/world/
//     local), and the pivot pair, driving ManipulatorSession;
//   - grid: snap, spacing, and the grid-frame verbs;
//   - the author -> cook -> play loop (Cook/Play/Stop), driven by callbacks the
//     host supplies so the toolbar stays free of project/PIE dependencies.
// Drawn by EditorUiFeature below the main menu bar via BeginViewportSideBar, so
// it reserves work-area space the viewport automatically avoids.
class EditorToolbar
{
public:
    // Host wiring for the Cook/Play/Stop group. Any callback may be empty (the
    // button is then disabled); IsPlaying gates Play vs Stop.
    struct PlayControls
    {
        std::function<void()> Cook;
        std::function<void()> Play;
        std::function<void()> Stop;
        std::function<bool()> IsPlaying;
    };

    // Host wiring for the grid-frame verbs (origin/align/rotate/reset). The
    // toolbar edits spacing and snap directly through GridSettings; frame verbs
    // need scene access, so they stay workspace-side behind callbacks.
    struct GridFrameControls
    {
        std::function<void()> OriginToSelection;
        std::function<void()> AlignToFace;
        std::function<void()> RotateInPlane; // one 90 degree step
        std::function<void()> Reset;
        std::function<void()> ToggleMoveOrigin; // Move gizmo drags the grid origin
        std::function<bool()> IsMovingOrigin;
    };

    // Host wiring for the transform group. The session drives gizmo mode/space
    // and the pivot toggles; SetOriginToPivot commits the moved pivot into the
    // primary brush's origin; HasSelection gates the pivot pair's visibility.
    struct TransformControls
    {
        ManipulatorSession* Session = nullptr;
        std::function<void()> SetOriginToPivot;
        std::function<bool()> HasSelection;
    };

    EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit, GridSettings& grid,
                  BrushCreationSettings& brushCreate, EdgeCutSettings& edgeCut);

    void SetPlayControls(PlayControls controls) { Play = std::move(controls); }
    void SetGridFrameControls(GridFrameControls controls) { GridFrame = std::move(controls); }
    void SetTransformControls(TransformControls controls) { Transform = std::move(controls); }

    void Draw();

private:
    void DrawToolContextGroup(float buttonSize); // brush primitive / edge-cut sub-modes
    void DrawModeGroup(float buttonSize);
    void DrawTransformGroup(float buttonSize);
    void DrawGridGroup(float buttonSize);
    void DrawPlayGroup(float buttonSize);

    ToolRegistry& Tools;
    MeshEditService& MeshEdit;
    GridSettings& Grid;
    BrushCreationSettings& BrushCreate;
    EdgeCutSettings& EdgeCut;
    PlayControls Play;
    GridFrameControls GridFrame;
    TransformControls Transform;
};
