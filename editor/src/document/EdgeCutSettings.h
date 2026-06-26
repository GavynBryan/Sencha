#pragma once

// Editor-wide edge-cut settings: whether the cut tool rings the whole edge loop or
// splits just the hovered edge. Owned by EditorWorkspace, read by the EdgeCutTool
// and toggled by the toolbar and the tool's hotkey. Mirrors BrushCreationSettings:
// one shared struct threaded by reference, not per-tool state.
struct EdgeCutSettings
{
    bool LoopCut = true;
};
