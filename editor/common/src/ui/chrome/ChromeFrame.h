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
// Where a frame mounts its ornaments. A panel's well is covered by whatever the
// body draws into it -- a viewport's scene image paints straight over anything
// there -- so a frame that wants visible hardware mounts it on the metal ring
// instead.
enum class OrnamentMount
{
    Well,
    Ring,
};

// Which header plate a composition carries.
enum class HeaderPlate
{
    Plain,
    Bezel, // a wider cap, the weight's chamfer, and a lit top-centre accent
};

// Everything a composition implies, in one lookup. Adding a field here is how a
// new visual trait reaches every consumer; adding a branch on PanelStyle in a
// painter is how this stops working.
struct PanelChromeSpec
{
    FrameSpec Frame;
    ImVec2 ContentPadding{};
    OrnamentMount Mount = OrnamentMount::Well;
    HeaderPlate Header = HeaderPlate::Plain;
    float HeaderHeight = 0.0f;  // the row a panel gives a titled header
    float RailCapScale = 3.0f;  // cap width on a docked panel's rail, in rail heights
    bool CornerBrackets = false;
};

[[nodiscard]] PanelChromeSpec ChromeSpecFor(PanelStyle style);

void DrawFrameBase(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style);

// The frame's edges over whatever the body drew: ring, sheen, bevel, well
// inset, corner cap, and the focus glow.
void DrawFrameEdges(ImDrawList* dl, ImVec2 mn, ImVec2 mx, PanelStyle style, bool focused);
} // namespace EditorChrome
