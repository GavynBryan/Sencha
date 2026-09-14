#pragma once

#include "icons/IconId.h"

#include <imgui.h>

#include <string>

// The editor's icons are SVG files under editor/icons, one per IconId. They
// are rasterized once at startup into the ImGui font atlas at a few sampling
// sizes; a control asks for an IconId and a tint and never learns what drew
// it.
namespace EditorChrome
{
// What the shell's atlas art is made of. These are its *only* invalidation
// inputs: when none of them change, neither does the atlas. Theme colors,
// metrics, decor and surfaces are deliberately absent -- a theme switch must
// not rebuild fonts -- and so is any theme geometry, which is why the mark is
// baked at a fixed height and scaled at draw time rather than baked to fit a
// nameplate whose size a metric could change.
struct ShellAtlasKey
{
    float UiScale = 1.0f;
    std::string LogoPath; // an SVG, flat white; empty = no mark

    bool operator==(const ShellAtlasKey&) const = default;
};

struct ShellAtlasResult
{
    int Icons = 0;
    bool Logo = false;
};

// Rasterizes every icon's file and the shell's mark into `atlas`, which it
// builds. One operation on purpose: every custom rect is reserved, the atlas is
// built exactly once, and only then is anything written, so nothing can repack
// the atlas after UVs have been recorded. Call after the fonts have been added
// and before the backend uploads the atlas.
//
// An icon whose file is missing, unparsable, or blank falls back to its Font
// Awesome glyph at draw time; a missing mark simply draws nothing.
ShellAtlasResult BakeAtlasArt(ImFontAtlas& atlas, const ShellAtlasKey& key);

// Draws `id` in `tint` as a centered square filling the shorter side of
// [mn, mx], from the baked raster that best samples that size, or from the
// glyph when nothing was baked for the current atlas.
void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint);

// Width over height of the baked mark, or 0 when none is baked. A caller sizes
// the box it wants from this; the mark scales into whatever box it is given.
[[nodiscard]] float LogoAspect();

// Draws the mark filling [mn, mx], in `tint`. The art is a flat white
// silhouette baked as coverage, exactly like an icon, so it takes the theme's
// accent at draw time and a theme change never rebakes it.
void DrawLogo(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
