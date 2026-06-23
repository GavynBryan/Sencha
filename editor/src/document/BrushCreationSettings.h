#pragma once

#include "../brush/BrushOps.h" // BrushPrimitive

// Editor-wide brush-creation settings: the active create sub-mode and the
// cylinder side count. Owned by EditorWorkspace, read by the create-drag
// interaction (to pick the generator) and surfaced/edited by the toolbar. Mirrors
// GridSettings: one shared struct threaded by reference, not per-tool state.
struct BrushCreationSettings
{
    BrushPrimitive ActivePrimitive = BrushPrimitive::Box;
    int CylinderSides = 12;
};
