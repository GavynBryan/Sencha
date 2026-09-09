#pragma once

#include "ChromeGeometry.h"
#include "PanelStyle.h"

#include <imgui.h>

// A panel's frame: the chamfered metal plate around a recessed content well,
// in the weight the panel asked for. The base goes under the content, the
// edges over it (content scrolls under the ring and the rail), so a panel's
// body never has to keep clear of its own chrome.
namespace EditorChrome
{
// The frame dimensions for a weight, in screen pixels: the theme's metrics
// scaled by the weight's row and by the UI scale.
FrameSpec SpecFor(PanelStyle style);

// The window padding a panel needs so its widgets start inside the ring.
ImVec2 ContentPadding(PanelStyle style);

// The recessed well, drawn before the content.
void DrawFrameBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style);

// The ring, its bevel, the well's inset edge, and the focus glow, drawn after
// the content.
void DrawFrameEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, bool focused);
} // namespace EditorChrome
