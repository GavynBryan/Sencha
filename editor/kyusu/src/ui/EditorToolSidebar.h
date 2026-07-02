#pragma once

#include <functional>

class ToolRegistry;

// The left tool strip (fixed app chrome, not a dockable panel): one icon button
// per registered ITool (Select/Brush/EdgeCut/Camera), active-lit, tooltip on
// hover. Replaces the old text-list Tools panel; drawn by EditorUiFeature via
// BeginViewportSideBar so it reserves work-area space the viewport avoids.
class EditorToolSidebar
{
public:
    // The tool registry is rebuilt when the workspace resets interaction
    // state, so the composition root injects a resolver instead of a reference.
    explicit EditorToolSidebar(std::function<ToolRegistry*()> tools);

    void Draw();

private:
    [[nodiscard]] ToolRegistry& Tools() const;

    std::function<ToolRegistry*()> ToolsResolver;
};
