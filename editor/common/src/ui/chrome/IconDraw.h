#pragma once

#include "icons/IconId.h"

#include <imgui.h>

// The editor's icons are SVG files under editor/icons, one per IconId. They
// are rasterized once at startup into the ImGui font atlas at a few sampling
// sizes; a control asks for an IconId and a tint and never learns what drew
// it.
namespace EditorChrome
{
// Rasterizes every icon's file into `atlas` (which it builds) so DrawIcon can
// blit them. Call once after the fonts have been added and before the backend
// uploads the atlas. Returns how many icons rasterized to something; an icon
// whose file is missing, unparsable, or blank falls back to its Font Awesome
// glyph at draw time.
int BakeIcons(ImFontAtlas& atlas, float uiScale);

// Draws `id` in `tint` as a centered square filling the shorter side of
// [mn, mx], from the baked raster that best samples that size, or from the
// glyph when nothing was baked for the current atlas.
void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
