#pragma once

#include "brush/BrushOps.h" // BrushPrimitive

// Editor-wide brush-creation settings: the active create sub-mode and the
// per-primitive generator parameters. Owned by EditorWorkspace, read by the
// create-drag interaction (to pick the generator) and surfaced/edited by the
// Tool Properties panel. Mirrors GridSettings: one shared struct threaded by
// reference, not per-tool state.
struct BrushCreationSettings
{
    BrushPrimitive ActivePrimitive = BrushPrimitive::Box;
    int CylinderSides = 12;
    int PlaneSubdivisions = 1;
    // Inner builds the primitive inside-out (all normals flipped): a room seen
    // from within instead of a solid seen from outside.
    bool Inner = false;
};
