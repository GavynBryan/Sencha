#pragma once

#include "ChromeGeometry.h"

#include <imgui.h>

// The chrome's machined decorations: screws, vents, slashes, LEDs, grooves,
// seams, grids. Placement is decided by ChromeGeometry; this only paints a
// kind into a rect, from the palette's metals and the tint it is given.
namespace EditorChrome
{
// Draws `kind` into [mn, mx]. `tint` is the accent the ornament lights with;
// its metal comes from the palette.
void DrawOrnament(ImDrawList* dl, OrnamentKind kind, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
