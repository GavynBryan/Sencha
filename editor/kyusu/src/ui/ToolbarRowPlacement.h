#pragma once

#include "ui/chrome/ChromeBars.h"

#include <algorithm>

// Where the viewport toolbar's blocks go on a bar channel. Kyusu's policy over
// the chassis's row: the gizmo strip's midpoint is the x the host names (the
// window's midline, where attention rests, rather than the pane's own), full
// stop, so a user's hand finds Move, Rotate and Scale in the same place
// whatever the active tool put on the left and wherever the pane is docked.
// The strip's tail (the gizmo space and the pivot pair) rides on its left and
// must clear the left block; the grid block at the right end must clear the
// strip. When the blocks cannot coexist with the strip on that midline, or the
// midline is not in the channel, the row is not nudged: it is laid out as the
// narrow row, every block in order from the left end, and reads as such.
//
// Pure: widths, a channel and a midline in, x positions out, so the rule is
// tested without a window.
namespace ToolbarRow
{
struct Widths
{
    float Left = 0.0f;  // the active tool's contextual controls
    float Strip = 0.0f; // the gizmo mode buttons, the block that is centred
    float Tail = 0.0f;  // what precedes the strip in its module
    float Right = 0.0f; // the grid group
};

struct Placement
{
    float Left = 0.0f;   // x where each block starts; Center is the strip's
    float Center = 0.0f; // own left edge, the tail lying to its left
    float Right = 0.0f;
    bool Centered = false; // the strip sits on the channel midpoint
};

[[nodiscard]] inline Placement Place(float channelMin, float channelMax, const Widths& w, float gap, float centerX)
{
    // The outer blocks keep the channel's ends whatever happens to the strip.
    const EditorChrome::BarRowRects row =
        EditorChrome::BarRowLayout(channelMin, channelMax, w.Left, w.Strip, w.Right, gap);
    const float stripMin = centerX - std::max(0.0f, w.Strip) * 0.5f;
    const float stripMax = stripMin + std::max(0.0f, w.Strip);
    const float tailStart = stripMin - std::max(0.0f, w.Tail);
    const bool leftClear = w.Left > 0.0f ? tailStart - gap >= row.LeftMax : tailStart >= channelMin;
    const bool rightClear = w.Right > 0.0f ? stripMax + gap <= row.RightMin : stripMax <= channelMax;
    if (leftClear && rightClear)
        return { .Left = row.LeftMin, .Center = stripMin, .Right = row.RightMin, .Centered = true };

    // The narrow row: in order from the left, each block after the last with
    // the gap between, a block of no width taking no gap. What runs past the
    // channel is the host's clip.
    Placement p;
    float x = channelMin;
    p.Left = x;
    if (w.Left > 0.0f)
        x += w.Left + gap;
    x += std::max(0.0f, w.Tail);
    p.Center = x;
    x += std::max(0.0f, w.Strip);
    if (w.Strip + w.Tail > 0.0f)
        x += gap;
    p.Right = x;
    p.Centered = false;
    return p;
}
} // namespace ToolbarRow
