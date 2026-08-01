#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class ManipulatorSession;
class ToolRegistry;
class MeshEditService;
struct GridSettings;
struct WorldViewSettings;

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
    // Host wiring for the cook profile split button and Play/Stop group.
    struct PlayControls
    {
        struct ProfileChoice
        {
            std::string Id;
            std::string Name;
            bool BuiltIn = false;
        };

        std::function<void()> RunCook;
        std::function<void()> CancelCook;
        std::function<void()> RebuildCook;
        std::function<bool()> IsCooking;
        std::function<std::vector<ProfileChoice>()> Profiles;
        std::function<std::string()> SelectedProfileId;
        std::function<void(std::string_view)> SelectProfile;
        std::function<void()> OpenProfiles;
        std::function<std::string()> CookStatus;
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
    // and the pivot toggles; the SetOrigin* callbacks re-origin the primary
    // brush (pivot commit, first selected vertex, world-bounds center/min
    // corner); HasSelection gates the pivot pair's visibility.
    struct TransformControls
    {
        std::function<void()> SetOriginToPivot;
        std::function<void()> SetOriginToVertex;
        std::function<void()> SetOriginToBoundsCenter;
        std::function<void()> SetOriginToBoundsCorner;
        std::function<bool()> HasSelection;
    };

    // The registry and session are resolved at call time rather than held: the
    // workspace stands them up during bring-up, after the toolbar is built.
    EditorToolbar(std::function<ToolRegistry*()> tools,
                  std::function<ManipulatorSession*()> session,
                  MeshEditService& meshEdit, GridSettings& grid,
                  WorldViewSettings& worldView);

    void SetPlayControls(PlayControls controls) { Play = std::move(controls); }
    void SetGridFrameControls(GridFrameControls controls) { GridFrame = std::move(controls); }
    void SetTransformControls(TransformControls controls) { Transform = std::move(controls); }

    void Draw();

private:
    void DrawToolContextGroup(float buttonSize); // edge-cut sub-mode / carve apply-cancel
    void DrawTransformGroup(float buttonSize);
    void DrawGridGroup(float buttonSize);
    void DrawPlayGroup(float buttonSize);

    [[nodiscard]] ToolRegistry& Tools() const;
    [[nodiscard]] ManipulatorSession* Session() const;

    std::function<ToolRegistry*()> ToolsResolver;
    std::function<ManipulatorSession*()> SessionResolver;
    MeshEditService& MeshEdit;
    GridSettings& Grid;
    WorldViewSettings& WorldView;
    PlayControls Play;
    GridFrameControls GridFrame;
    TransformControls Transform;
};
