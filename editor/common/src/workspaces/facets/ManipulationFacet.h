#pragma once

#include <string>

// How the workspace's hands act on what they hold: what a click selects, the
// gizmo's mode and axes, the grid, whether snapping is on. The settings
// themselves are actions (their categories: Mode, Transform, Grid), so a
// readout names them and a popover lists them.
struct WorkspaceManipulationFacet
{
    // Empty when the workspace manipulates nothing.
    std::string Element = {};
    std::string Mode = {};
    std::string Space = {};
    std::string Grid = {};
    bool Snap = false;
    bool Pivot = false; // the gizmo is moving the pivot, not the selection
};
