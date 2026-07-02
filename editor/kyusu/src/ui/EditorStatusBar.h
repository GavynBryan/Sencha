#pragma once

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
    EditorStatusBar(ToolRegistry& tools, ViewportLayout& layout, SelectionService& selection,
                    const GridSettings& grid, MeshEditService& meshEdit,
                    const ManipulatorSession& manipulators);

    void Draw();

private:
    ToolRegistry& Tools;
    ViewportLayout& Layout;
    SelectionService& Selection;
    const GridSettings& Grid;
    MeshEditService& MeshEdit;
    const ManipulatorSession& Manipulators;
};
