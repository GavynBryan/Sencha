#pragma once

#include "viewport/ViewportId.h"

#include <imgui.h>

// Transient rubber-band selection rectangle. Written by the select tool during a
// drag, read by the viewport panel to draw the overlay. Screen-pixel coordinates
// (same space as pointer events and ImGui). Active only once the drag passes the
// click/box threshold. (08-select-tool-v2.md)
struct MarqueeState
{
    bool Active = false;
    ImVec2 Start = {};
    ImVec2 Current = {};
    ViewportId Viewport = {};
};
