#pragma once

#include "ChromeGeometry.h"
#include "ChromeSources.h"

#include <imgui.h>

// The ornament table and the one draw call over it: screws, vents, slashes,
// stripes, LEDs, grooves, seams, strips, scanlines. Placement is decided by
// ChromeGeometry; this only paints a kind into a rect. Each kind is a table
// row an authored sprite can replace.
namespace EditorChrome
{
[[nodiscard]] const OrnamentSource& OrnamentSourceFor(OrnamentKind kind);
void SetOrnamentSource(OrnamentKind kind, const OrnamentSource& source);
void ResetOrnamentSources();

// Draws `kind` into [mn, mx]. `tint` is the accent the ornament lights with;
// its metal comes from the palette.
void DrawOrnament(ImDrawList* dl, OrnamentKind kind, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
