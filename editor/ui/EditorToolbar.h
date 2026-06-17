#pragma once

class ToolRegistry;
class MeshEditService;

// The top icon toolbar (fixed app chrome, not a dockable panel). Two backed
// control groups, each with an active-state highlight:
//   - tools: the registered ITools (Select/Brush/Camera), driving ToolRegistry;
//   - mesh element mode: Object/Vertex/Edge/Face, driving MeshEditService.
// Drawn by EditorUiFeature below the main menu bar via BeginViewportSideBar, so
// it reserves work-area space the viewport automatically avoids.
class EditorToolbar
{
public:
    EditorToolbar(ToolRegistry& tools, MeshEditService& meshEdit);

    void Draw();

private:
    ToolRegistry& Tools;
    MeshEditService& MeshEdit;
};
