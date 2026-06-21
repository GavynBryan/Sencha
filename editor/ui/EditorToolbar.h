#pragma once

#include <functional>

class ToolRegistry;
class MeshEditService;
struct GridSettings;

// The top icon toolbar (fixed app chrome, not a dockable panel). Backed control
// groups, each with an active-state highlight:
//   - tools: the registered ITools (Select/Brush/Camera), driving ToolRegistry;
//   - mesh element mode: Object/Vertex/Edge/Face, driving MeshEditService;
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

    EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit, GridSettings& grid);

    void SetPlayControls(PlayControls controls) { Play = std::move(controls); }

    void Draw();

private:
    ToolRegistry& Tools;
    MeshEditService& MeshEdit;
    GridSettings& Grid;
    PlayControls Play;
};
