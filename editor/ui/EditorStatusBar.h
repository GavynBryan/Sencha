#pragma once

class ToolRegistry;
class ViewportLayout;
class SelectionService;

// The bottom status bar (fixed app chrome). Read-only, all values backed by live
// editor state: active tool, active viewport orientation + grid spacing, selection
// count, and a wall clock. No toggles — controls that imply state we don't have
// yet (snap-enable, angle snap) are deliberately omitted rather than faked.
class EditorStatusBar
{
public:
    EditorStatusBar(ToolRegistry& tools, ViewportLayout& layout, SelectionService& selection);

    void Draw();

private:
    ToolRegistry& Tools;
    ViewportLayout& Layout;
    SelectionService& Selection;
};
