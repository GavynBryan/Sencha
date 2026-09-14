#pragma once

#include "ChromeFrame.h"
#include "ChromeGeometry.h"
#include "PanelStyle.h"
#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <string_view>

// Headers as their own mechanical part: a left accent cap, a title, a line
// that stretches to whatever width is left, and a control region at the right.
// Three shapes share that layout: the rail a docked panel carries under its
// tab (no title; the tab has it), the full titled row (a viewport's
// orientation row, a floating window), and the labelled rule that titles a
// section inside a panel.
namespace EditorChrome
{
struct HeaderState
{
    bool Focused = false;  // the panel is the one being worked in
    bool Selected = false; // the header names the thing being edited: amber over cyan
};

// Cap + line across [mn, mx], the rail at the top of a panel's well. The cap
// is sized by the panel weight; the line stops ornamentWidth short of the
// right end for the ornaments placed there.
void DrawHeaderRail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, HeaderState state,
                    float ornamentWidth = 0.0f);

// What a titled row should look like and what it must make room for. The style
// carries the plate's weight and whether it is a plain row or a bezel; CapWidth
// overrides the style's own cap, for a caller mounting something of its own
// there (the shell's nameplate puts its mark in the cap).
struct HeaderRowSpec
{
    PanelStyle Style = PanelStyle::Standard;
    // 0 = the style's own accent cap. Any other value reserves a cap that
    // wide and leaves it empty: the region belongs to the caller, which draws
    // whatever it mounts there.
    float CapWidth = 0.0f;
    float ReservedControlWidth = 0.0f;  // free region at the right for the caller's widgets
    // Whether the rule runs from the title to the control region. A plate
    // that is a mount for controls rather than a title reads cleaner bare.
    bool Rule = true;
};

// A chamfered plate carrying cap, title, line, and the spec's free control
// region at the right. Returns the regions it laid out so the caller can place
// a widget in the control one, or draw into the cap.
HeaderRegions DrawHeaderRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                            HeaderState state, const HeaderRowSpec& spec);

// The height a titled row should be for this composition. A panel asks rather
// than knowing why a bezel wants more room than a plain row.
[[nodiscard]] float HeaderRowHeight(PanelStyle style);

// The plate width a row of `height` needs to hold its cap, `titleWidth` of
// title, and a rule at least `minLineWidth` long. The one place that knows the
// packing, so a caller sizing its own plate cannot drift from what the row
// actually draws.
[[nodiscard]] float HeaderRowWidth(const HeaderRowSpec& spec, float height, float titleWidth,
                                   float minLineWidth, float gap);

// Cap + title + line with no plate, for a section inside a panel.
void DrawHeaderRule(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                    HeaderState state);

// The item-sized section title: a DrawHeaderRule across the content width at
// the cursor, advancing the cursor past it. Replaces a dimmed text label.
// Call immediately after a stock CollapsingHeader, before overlapping controls.
void HeaderNotch();

void SectionTitle(const char* label);
} // namespace EditorChrome
