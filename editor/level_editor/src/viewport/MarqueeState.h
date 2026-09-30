#pragma once

#include "viewport/ViewportId.h"

#include <math/Vec.h>

// Transient rubber-band selection rectangle. Written by the select tool during a
// drag, read by the viewport panel to draw the overlay. Screen-pixel coordinates
// (same space as pointer events and ImGui). Active only once the drag passes the
// click/box threshold. (08-select-tool-v2.md)
struct MarqueeState
{
    bool Active = false;
    Vec2d Start = {};
    Vec2d Current = {};
    ViewportId Viewport = {};
};
