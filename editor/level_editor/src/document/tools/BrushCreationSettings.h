#pragma once

#include "brush/BrushOps.h" // BrushPrimitive

// Brush-creation settings: the active create sub-mode and the per-primitive
// generator parameters. A member of BrushTool, which outlives any one
// document, so they survive a document swap; read by the create-drag
// interaction (to pick the generator) and driven by the tool's properties
// row and its variants on the tool wheel.
struct BrushCreationSettings
{
    BrushPrimitive ActivePrimitive = BrushPrimitive::Box;
    int CylinderSides = 12;
    int PlaneSubdivisions = 1;
    // Inner builds the primitive inside-out (all normals flipped): a room seen
    // from within instead of a solid seen from outside.
    bool Inner = false;
};
