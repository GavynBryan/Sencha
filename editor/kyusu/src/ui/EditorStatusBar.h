#pragma once

#include <functional>

class ManipulatorSession;
class MeshEditService;
class ToolRegistry;
class ViewportLayout;
class SelectionService;
struct GridSettings;

// The bottom status bar (fixed app chrome). Read-only, all values backed by live
// editor state: active tool, element mode, effective gizmo + space, selection
// count, active viewport, grid spacing/frame, and a wall clock. No toggles:
// every control lives in the toolbar; this bar only reports.
class EditorStatusBar
{
public:
    // The tool registry and manipulator session are rebuilt when the workspace
    // resets interaction state, so the composition root injects resolvers
    // instead of references.
    EditorStatusBar(std::function<ToolRegistry*()> tools,
                    std::function<const ManipulatorSession*()> manipulators,
                    ViewportLayout& layout, SelectionService& selection,
                    const GridSettings& grid, MeshEditService& meshEdit);

    void Draw();

private:
    [[nodiscard]] ToolRegistry& Tools() const;
    [[nodiscard]] const ManipulatorSession& Manipulators() const;

    std::function<ToolRegistry*()> ToolsResolver;
    std::function<const ManipulatorSession*()> ManipulatorsResolver;
    ViewportLayout& Layout;
    SelectionService& Selection;
    const GridSettings& Grid;
    MeshEditService& MeshEdit;
};
