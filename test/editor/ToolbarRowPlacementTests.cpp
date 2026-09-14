#include "ui/ToolbarRowPlacement.h"

#include <gtest/gtest.h>

// The viewport toolbar's row: the gizmo strip's midpoint is the midline the
// host names whatever the side blocks do, and when that cannot hold the row
// is the narrow row in order from the left, never a strip nudged off centre.
namespace
{
constexpr float kMin = 100.0f;
constexpr float kMax = 1100.0f; // midpoint 600
constexpr float kGap = 12.0f;
constexpr float kMid = 600.0f; // the midline, here the channel's own

float StripMid(const ToolbarRow::Placement& p, const ToolbarRow::Widths& w)
{
    return p.Center + w.Strip * 0.5f;
}
}

TEST(ToolbarRowPlacement, TheStripSitsOnTheMidpointWithNoSideBlocks)
{
    const ToolbarRow::Widths w{ .Strip = 110.0f };
    const ToolbarRow::Placement p = ToolbarRow::Place(kMin, kMax, w, kGap, kMid);
    EXPECT_TRUE(p.Centered);
    EXPECT_FLOAT_EQ(StripMid(p, w), 600.0f);
    EXPECT_FLOAT_EQ(p.Left, kMin);
    EXPECT_FLOAT_EQ(p.Right, kMax);
}

TEST(ToolbarRowPlacement, AnyLeftBlockThatFitsLeavesTheStripExactlyWhereItWas)
{
    ToolbarRow::Widths w{ .Strip = 110.0f, .Tail = 130.0f, .Right = 300.0f };
    // Room left of the tail: 545 - 130 - 100 - gap = 303.
    for (float left = 0.0f; left <= 300.0f; left += 10.0f)
    {
        w.Left = left;
        const ToolbarRow::Placement p = ToolbarRow::Place(kMin, kMax, w, kGap, kMid);
        ASSERT_TRUE(p.Centered) << "left " << left;
        EXPECT_FLOAT_EQ(StripMid(p, w), 600.0f) << "left " << left;
        EXPECT_FLOAT_EQ(p.Right, kMax - 300.0f);
    }
}

TEST(ToolbarRowPlacement, TheFirstLeftWidthThatWouldNudgeTheStripFlowsTheRowInstead)
{
    ToolbarRow::Widths w{ .Strip = 110.0f, .Tail = 130.0f, .Right = 300.0f };
    w.Left = 310.0f; // the tail would need to slide right
    const ToolbarRow::Placement p = ToolbarRow::Place(kMin, kMax, w, kGap, kMid);
    EXPECT_FALSE(p.Centered);
    // In order from the left, nothing overlapping: left, tail, strip, right.
    EXPECT_FLOAT_EQ(p.Left, kMin);
    EXPECT_FLOAT_EQ(p.Center, kMin + 310.0f + kGap + 130.0f);
    EXPECT_FLOAT_EQ(p.Right, p.Center + 110.0f + kGap);
    EXPECT_NE(StripMid(p, w), 600.0f);
}

TEST(ToolbarRowPlacement, ARightBlockThatWouldRunIntoTheStripFlowsTheRow)
{
    ToolbarRow::Widths w{ .Strip = 110.0f, .Tail = 130.0f, .Right = 430.0f };
    // Strip ends at 655; right starts at 670: clear by 15 > gap.
    EXPECT_TRUE(ToolbarRow::Place(kMin, kMax, w, kGap, kMid).Centered);
    w.Right = 440.0f; // the gap no longer fits
    EXPECT_FALSE(ToolbarRow::Place(kMin, kMax, w, kGap, kMid).Centered);
    // Without a left block the tail only has to stay in the channel.
    w.Right = 300.0f;
    w.Tail = 440.0f;
    EXPECT_TRUE(ToolbarRow::Place(kMin, kMax, w, kGap, kMid).Centered);
    w.Tail = 450.0f;
    EXPECT_FALSE(ToolbarRow::Place(kMin, kMax, w, kGap, kMid).Centered);
}

TEST(ToolbarRowPlacement, APaneOfOrdinaryWidthStaysCentredWithTheRealControlSet)
{
    // Roughly Kyusu's row: a tool context of a few buttons, the gizmo space
    // combo and pivot pair, the grid group of buttons and combos, on a pane
    // a little over a thousand pixels wide.
    const ToolbarRow::Widths w{ .Left = 120.0f, .Strip = 116.0f, .Tail = 150.0f, .Right = 400.0f };
    const ToolbarRow::Placement p = ToolbarRow::Place(0.0f, 1000.0f, w, kGap, 500.0f);
    EXPECT_TRUE(p.Centered);
    EXPECT_FLOAT_EQ(StripMid(p, w), 500.0f);
}

TEST(ToolbarRowPlacement, TheNarrowRowSkipsTheGapOfAnAbsentBlock)
{
    const ToolbarRow::Widths w{ .Left = 0.0f, .Strip = 110.0f, .Tail = 0.0f, .Right = 900.0f };
    const ToolbarRow::Placement p = ToolbarRow::Place(kMin, kMax, w, kGap, kMid);
    ASSERT_FALSE(p.Centered);
    EXPECT_FLOAT_EQ(p.Center, kMin);
    EXPECT_FLOAT_EQ(p.Right, kMin + 110.0f + kGap);
}

TEST(ToolbarRowPlacement, TheMidlineIsTheHostsAndNeedNotBeThePanes)
{
    const ToolbarRow::Widths w{ .Left = 60.0f, .Strip = 110.0f, .Tail = 130.0f, .Right = 300.0f };
    // A pane docked off to one side: the window's midline falls left of the
    // pane's own, and the strip sits on it as long as the blocks clear.
    const ToolbarRow::Placement p = ToolbarRow::Place(kMin, kMax, w, kGap, 450.0f);
    ASSERT_TRUE(p.Centered);
    EXPECT_FLOAT_EQ(StripMid(p, w), 450.0f);
    EXPECT_FLOAT_EQ(p.Right, kMax - 300.0f);
    // A midline the pane does not contain, or one the tail cannot reach past
    // the left block, is the narrow row.
    EXPECT_FALSE(ToolbarRow::Place(kMin, kMax, w, kGap, 50.0f).Centered);
    EXPECT_FALSE(ToolbarRow::Place(kMin, kMax, w, kGap, 250.0f).Centered);
    EXPECT_FALSE(ToolbarRow::Place(kMin, kMax, w, kGap, 1090.0f).Centered);
}
