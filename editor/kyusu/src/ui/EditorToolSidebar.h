#pragma once

class ToolRegistry;

// The left tool strip (fixed app chrome, not a dockable panel): one icon button
// per registered ITool (Select/Brush/EdgeCut/Camera), active-lit, tooltip on
// hover. Replaces the old text-list Tools panel; drawn by EditorUiFeature via
// BeginViewportSideBar so it reserves work-area space the viewport avoids.
class EditorToolSidebar
{
public:
    explicit EditorToolSidebar(ToolRegistry& tools);

    void Draw();

private:
    ToolRegistry& Tools;
};
