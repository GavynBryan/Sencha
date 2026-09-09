#pragma once

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
    bool Focused = false; // the panel is the one being worked in
};

// Cap + line across [mn, mx], the rail at the top of a panel's well. The line
// stops ornamentWidth short of the right end for the ornaments placed there.
void DrawHeaderRail(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, HeaderState state,
                    float ornamentWidth = 0.0f);

// A chamfered plate carrying cap, title, line, and a free control region of
// reservedControlWidth at the right for the caller's own widgets. Returns the
// regions it laid out so the caller can place a widget in the control one.
HeaderRegions DrawHeaderRow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                            HeaderState state, float reservedControlWidth);

// Cap + title + line with no plate, for a section inside a panel.
void DrawHeaderRule(ImDrawList* dl, ImVec2 mn, ImVec2 mx, std::string_view title, EditorUi::TextRole role,
                    HeaderState state);

// The item-sized section title: a DrawHeaderRule across the content width at
// the cursor, advancing the cursor past it. Replaces a dimmed text label.
void SectionTitle(const char* label);
} // namespace EditorChrome
