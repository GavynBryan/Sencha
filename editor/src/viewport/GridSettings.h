#pragma once

// Editor-wide grid/snap settings — the single source for grid spacing and the
// snap-on/off toggle. Owned by LevelWorkspace and consulted by EditorViewport::
// GetGrid(), which stamps these onto the GridPlane it returns, so every snap
// consumer (picking, translate/bounds manipulators, brush-create) and the grid
// renderer honor one shared setting. Surfaced/edited by the toolbar.
struct GridSettings
{
    bool SnapEnabled = true;
    float Spacing = 1.0f;
};
