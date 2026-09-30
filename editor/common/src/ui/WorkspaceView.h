#pragma once

#include "IEditorPanel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Fraction of its parent split each DockSlot region takes when the default
// layout is built. Regions without panels are never split, so the fields for
// slots a workspace leaves empty are inert.
struct DockLayoutRatios
{
    float Bottom = 0.19f;       // full-width strip, of the whole dockspace height
    float LeftEdge = 0.05f;    // tool column, of the main row width
    float Left = 0.18f;         // left column, of the main row width
    float Right = 0.24f;        // right column, of the width left after the left column
    float CenterBottom = 0.26f; // strip under the central node, of the center column height
    float RightBottom = 0.285f; // lower right, of the right column height
};

// The File menu entries a workspace answers; an unset one is shown disabled,
// and New World only appears when set.
struct WorkspaceFileActions
{
    std::function<void()> New;
    std::function<void()> NewWorld;
    std::function<void()> Open;
    std::function<void()> Save;
    std::function<void()> SaveAs;
};

// Undo and redo for a workspace that keeps its own history; unset, the Edit
// menu uses the window's.
struct WorkspaceEditActions
{
    std::function<void()> Undo;
    std::function<void()> Redo;
    std::function<bool()> CanUndo;
    std::function<bool()> CanRedo;
};

// Controls a workspace mounts at the right end of the window's tab strip,
// drawn at the cursor. Lit is whether the strip should read as busy.
struct WorkspaceBarControls
{
    std::function<float()> Width;
    std::function<void()> Draw;
    std::function<bool()> Lit;
};

// What the window host keeps per view between frames to lay its dockspace out.
struct WorkspaceDockState
{
    bool LayoutDirty = false;
    bool PlacementChecked = false;
    std::vector<std::string> PendingTabFocus;
};

// The ImGui half of a workspace: what a window draws for it while it is the
// active one, and nothing while it is not (editor/ARCHITECTURE.md).
struct WorkspaceView
{
    std::vector<std::unique_ptr<IEditorPanel>> Panels;
    // Bars drawn after the caption and before the panels; one that reserves
    // space shrinks the work area the panels get. Insertion order is draw order.
    std::vector<std::function<void()>> Chrome;
    // Surfaces drawn after every panel that reserve no space.
    std::vector<std::function<void()>> Overlays;
    DockLayoutRatios Layout;
    WorkspaceFileActions File;
    WorkspaceEditActions Edit;
    WorkspaceBarControls BarControls;
    // The open document and whether it has unsaved edits, read each frame.
    std::function<std::string()> Status;

    WorkspaceDockState Dock;

    void AddPanel(std::unique_ptr<IEditorPanel> panel)
    {
        if (panel != nullptr)
            Panels.push_back(std::move(panel));
    }
};
