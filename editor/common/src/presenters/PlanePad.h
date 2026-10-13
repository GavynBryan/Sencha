#pragma once

#include "ui/UiScreenSlot.h"

#include <math/geometry/2d/Rect2d.h>
#include <ui/UiSurface.h>

#include <span>
#include <string>
#include <vector>

class UiService;

// A mark on the plane, 0 to 1 across and up, drawn as large as it weighs.
struct PlanePadMark
{
    Vec2d At = {};
    float Weight = 0.0f;
    std::string Label = {};
};

// Where a window point falls on a pad placed at `region`, 0 to 1 across and
// up, held to its edges; and back. A line has one range, across.
[[nodiscard]] Vec2d PlanePadValueAt(const Rect2d& region, bool line, Vec2d windowPoint);
[[nodiscard]] Vec2d PlanePadPointOf(const Rect2d& region, Vec2d at);

//=============================================================================
// PlanePad
//
// A point over a plane of labelled marks, on a workspace's content surface:
// a blendspace's samples and where it is, or anything placed over two ranges
// (or one, as a line). It only shows; the workspace takes the pointer over it
// and maps where it is with ValueAt.
//=============================================================================
class PlanePad
{
public:
    explicit PlanePad(UiService& ui);

    void MoveTo(UiSurfaceId content) { Screen.MoveTo(content); }
    // Per frame it shows: where, in window pixels, what is marked, the point,
    // and whether it is a line of one range.
    void Show(const Rect2d& region, std::span<const PlanePadMark> marks, Vec2d point, std::string caption,
              bool line);
    void Hide();

    [[nodiscard]] bool IsShowing() const { return Showing; }
    [[nodiscard]] bool Contains(Vec2d windowPoint) const;
    // A window point as a place on the plane, held to its edges.
    [[nodiscard]] Vec2d ValueAt(Vec2d windowPoint) const { return PlanePadValueAt(Region, Line, windowPoint); }

private:
    UiService& Ui;
    UiScreenSlot Screen;
    Rect2d Region{};
    bool Line = false;
    bool Showing = false;
};
