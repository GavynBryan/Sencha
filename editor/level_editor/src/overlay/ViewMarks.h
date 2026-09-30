#pragma once

#include "actions/ActionTone.h"
#include "icons/IconId.h"

#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <string>
#include <vector>

struct EditorViewport;
struct EditorOverlayState;
struct MarqueeState;

enum class ViewMarkKind : std::uint8_t
{
    Label,   // text whose top-left is at the rect's position
    Handle,  // a filled, bordered square
    Button,  // a pinned tool button
    Caption, // a line of text centred in the rect
    Tooltip, // what the hot button does, a line under its row
    Knob,    // the grip of a rotation dial
    Marquee, // the rubber band of a box selection
    Marker,  // an entity with no shape of its own, at its origin
};

// Something drawn at a point over a view, in window pixels: the part of the
// overlay a document places, as opposed to the strokes the view's own render
// draws along lines in the world.
struct ViewMark
{
    ViewMarkKind Kind = ViewMarkKind::Label;
    Rect2d Rect{};
    std::string Text = {};
    IconId Icon = IconId::None;
    Vec4 Color{};
    Vec4 Border{};
    ActionTone Tone = ActionTone::Normal;
    bool Hot = false;
    bool Enabled = true;
};

// Every mark `overlay` and `marquee` put over `viewport` this frame, projected
// through its camera into its rect. `scale` is the UI scale the tools lay
// their buttons out at, so a drawn button and a hit-tested one are the same.
void AppendViewMarks(const EditorOverlayState& overlay, const MarqueeState& marquee, const EditorViewport& viewport,
                     float scale, std::vector<ViewMark>& out);
