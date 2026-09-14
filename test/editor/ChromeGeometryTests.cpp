#include "ui/chrome/ChromeGeometry.h"

#include <gtest/gtest.h>

#include <array>

using namespace EditorChrome;

namespace
{
bool Inside(const OrnamentSlot& slot, ImVec2 mn, ImVec2 mx)
{
    return slot.Min.x >= mn.x && slot.Min.y >= mn.y && slot.Max.x <= mx.x && slot.Max.y <= mx.y
        && slot.Max.x > slot.Min.x && slot.Max.y > slot.Min.y;
}

bool Overlaps(const OrnamentSlot& a, const OrnamentSlot& b)
{
    return a.Min.x < b.Max.x && b.Min.x < a.Max.x && a.Min.y < b.Max.y && b.Min.y < a.Max.y;
}

int CountKind(std::span<const OrnamentSlot> slots, OrnamentKind kind)
{
    int n = 0;
    for (const OrnamentSlot& slot : slots)
        n += slot.Kind == kind ? 1 : 0;
    return n;
}
}

TEST(ChromeGeometry, ChamferOutlineIsAnOctagonClockwiseFromTheTopEdge)
{
    const ChamferPoly poly = ChamferOutline(ImVec2(10, 20), ImVec2(110, 70), 6.0f);
    ASSERT_EQ(poly.Count, 8);
    EXPECT_FLOAT_EQ(poly.P[0].x, 16); EXPECT_FLOAT_EQ(poly.P[0].y, 20);
    EXPECT_FLOAT_EQ(poly.P[1].x, 104); EXPECT_FLOAT_EQ(poly.P[1].y, 20);
    EXPECT_FLOAT_EQ(poly.P[2].x, 110); EXPECT_FLOAT_EQ(poly.P[2].y, 26);
    EXPECT_FLOAT_EQ(poly.P[3].x, 110); EXPECT_FLOAT_EQ(poly.P[3].y, 64);
    EXPECT_FLOAT_EQ(poly.P[4].x, 104); EXPECT_FLOAT_EQ(poly.P[4].y, 70);
    EXPECT_FLOAT_EQ(poly.P[5].x, 16); EXPECT_FLOAT_EQ(poly.P[5].y, 70);
    EXPECT_FLOAT_EQ(poly.P[6].x, 10); EXPECT_FLOAT_EQ(poly.P[6].y, 64);
    EXPECT_FLOAT_EQ(poly.P[7].x, 10); EXPECT_FLOAT_EQ(poly.P[7].y, 26);
}

TEST(ChromeGeometry, ZeroChamferIsTheRectangle)
{
    const ChamferPoly poly = ChamferOutline(ImVec2(0, 0), ImVec2(40, 30), 0.0f);
    ASSERT_EQ(poly.Count, 4);
    EXPECT_FLOAT_EQ(poly.P[2].x, 40);
    EXPECT_FLOAT_EQ(poly.P[2].y, 30);
}

TEST(ChromeGeometry, ChamferClampsToHalfTheShorterSide)
{
    // A chamfer larger than the rect collapses the short edges to points
    // rather than crossing over.
    const ChamferPoly poly = ChamferOutline(ImVec2(0, 0), ImVec2(100, 20), 50.0f);
    ASSERT_EQ(poly.Count, 8);
    EXPECT_FLOAT_EQ(poly.P[0].x, 10);
    EXPECT_FLOAT_EQ(poly.P[2].y, 10);
    EXPECT_FLOAT_EQ(poly.P[3].y, 10);
}

TEST(ChromeGeometry, DegenerateRectHasNoOutline)
{
    EXPECT_EQ(ChamferOutline(ImVec2(5, 5), ImVec2(5, 40), 4.0f).Count, 0);
    EXPECT_EQ(ChamferOutline(ImVec2(5, 5), ImVec2(40, 5), 4.0f).Count, 0);
    EXPECT_EQ(ChamferOutline(ImVec2(10, 10), ImVec2(0, 0), 4.0f).Count, 0);
}

TEST(ChromeGeometry, FrameLayoutInsetsTheWellByTheRingAndStacksTheRail)
{
    const FrameSpec spec{ .Chamfer = 6, .Border = 1, .Recess = 3, .Rail = 6 };
    const FrameRects rects = FrameLayout(ImVec2(100, 200), ImVec2(400, 500), spec);
    EXPECT_FLOAT_EQ(rects.WellMin.x, 104); EXPECT_FLOAT_EQ(rects.WellMin.y, 204);
    EXPECT_FLOAT_EQ(rects.WellMax.x, 396); EXPECT_FLOAT_EQ(rects.WellMax.y, 496);
    EXPECT_FLOAT_EQ(rects.RailMin.y, 204);
    EXPECT_FLOAT_EQ(rects.RailMax.y, 210);
    EXPECT_FLOAT_EQ(rects.RailMax.x, 396);

    const FrameSpec noRail{ .Chamfer = 3, .Border = 1, .Recess = 1, .Rail = 0 };
    const FrameRects flat = FrameLayout(ImVec2(0, 0), ImVec2(50, 50), noRail);
    EXPECT_FLOAT_EQ(flat.RailMax.y, flat.RailMin.y);

    // A rect smaller than the ring yields an empty, not inverted, well.
    const FrameRects tiny = FrameLayout(ImVec2(0, 0), ImVec2(4, 4), spec);
    EXPECT_GE(tiny.WellMax.x, tiny.WellMin.x);
    EXPECT_GE(tiny.WellMax.y, tiny.WellMin.y);
}

TEST(ChromeGeometry, BarLayoutStacksRimsChannelAndCentersTheLane)
{
    const BarSpec spec{ .Rim = 3.0f, .Clearance = 5.0f, .Cap = 20.0f };
    const BarRects bar = BarLayout(ImVec2(0.0f, 0.0f), ImVec2(400.0f, 39.0f), spec, 23.0f);

    EXPECT_FLOAT_EQ(bar.TopRimMin.y, 0.0f);
    EXPECT_FLOAT_EQ(bar.TopRimMax.y, 3.0f);
    EXPECT_FLOAT_EQ(bar.BottomRimMin.y, 36.0f);
    EXPECT_FLOAT_EQ(bar.BottomRimMax.y, 39.0f);
    // The rims span the whole band; the caps and channel divide what is left.
    EXPECT_FLOAT_EQ(bar.TopRimMin.x, 0.0f);
    EXPECT_FLOAT_EQ(bar.TopRimMax.x, 400.0f);

    EXPECT_TRUE(bar.HasCaps);
    EXPECT_FLOAT_EQ(bar.LeftCapMin.x, 0.0f);
    EXPECT_FLOAT_EQ(bar.LeftCapMax.x, 20.0f);
    EXPECT_FLOAT_EQ(bar.RightCapMin.x, 380.0f);
    EXPECT_FLOAT_EQ(bar.RightCapMax.x, 400.0f);
    // The caps fill the band between the rims.
    EXPECT_FLOAT_EQ(bar.LeftCapMin.y, 3.0f);
    EXPECT_FLOAT_EQ(bar.LeftCapMax.y, 36.0f);

    EXPECT_FLOAT_EQ(bar.ChannelMin.x, 20.0f);
    EXPECT_FLOAT_EQ(bar.ChannelMax.x, 380.0f);
    EXPECT_FLOAT_EQ(bar.ChannelMin.y, 3.0f);
    EXPECT_FLOAT_EQ(bar.ChannelMax.y, 36.0f);

    // The lane is the item height, centered in the channel and on whole pixels.
    EXPECT_FLOAT_EQ(bar.LaneMin.y, 8.0f);
    EXPECT_FLOAT_EQ(bar.LaneMax.y, 31.0f);
    EXPECT_FLOAT_EQ(bar.LaneMin.x, bar.ChannelMin.x);
    EXPECT_FLOAT_EQ(bar.LaneMax.x, bar.ChannelMax.x);
    const float laneCenter = (bar.LaneMin.y + bar.LaneMax.y) * 0.5f;
    EXPECT_FLOAT_EQ(laneCenter, 19.5f); // the bar's own centerline
}

TEST(ChromeGeometry, BarHeightForRoundTripsThroughBarLayout)
{
    const BarSpec spec{ .Rim = 3.0f, .Clearance = 5.0f, .Cap = 20.0f };
    for (const float itemHeight : { 12.0f, 23.0f, 28.75f, 40.0f })
    {
        const float height = BarHeightFor(spec, itemHeight);
        EXPECT_FLOAT_EQ(height, itemHeight + 16.0f);
        const BarRects bar = BarLayout(ImVec2(0.0f, 0.0f), ImVec2(400.0f, height), spec, itemHeight);
        // A bar of exactly this height seats the lane at Rim + Clearance and
        // gives it the full item height, whatever the scale made of the item.
        EXPECT_FLOAT_EQ(bar.LaneMin.y, 8.0f);
        EXPECT_FLOAT_EQ(bar.LaneMax.y - bar.LaneMin.y, itemHeight);
    }
}

TEST(ChromeGeometry, BarLayoutDropsCapsAndClampsRimsOnSmallBars)
{
    const BarSpec spec{ .Rim = 3.0f, .Clearance = 5.0f, .Cap = 20.0f };

    // Too narrow to carry a cap at each end: the channel takes the full width.
    const BarRects narrow = BarLayout(ImVec2(0.0f, 0.0f), ImVec2(60.0f, 39.0f), spec, 23.0f);
    EXPECT_FALSE(narrow.HasCaps);
    EXPECT_FLOAT_EQ(narrow.ChannelMin.x, 0.0f);
    EXPECT_FLOAT_EQ(narrow.ChannelMax.x, 60.0f);
    EXPECT_FLOAT_EQ(narrow.LeftCapMax.x, narrow.LeftCapMin.x);

    // Shorter than two rims: the rims split the band and the channel collapses
    // rather than inverting.
    const BarRects squat = BarLayout(ImVec2(0.0f, 0.0f), ImVec2(400.0f, 4.0f), spec, 23.0f);
    EXPECT_FLOAT_EQ(squat.TopRimMax.y, 2.0f);
    EXPECT_FLOAT_EQ(squat.BottomRimMin.y, 2.0f);
    EXPECT_GE(squat.ChannelMax.y, squat.ChannelMin.y);
    EXPECT_FLOAT_EQ(squat.LaneMax.y, squat.LaneMin.y);

    // A degenerate band lays out nothing.
    const BarRects empty = BarLayout(ImVec2(10.0f, 10.0f), ImVec2(10.0f, 40.0f), spec, 23.0f);
    EXPECT_FALSE(empty.HasCaps);
    EXPECT_FLOAT_EQ(empty.LaneMax.y, empty.LaneMin.y);
}

TEST(ChromeGeometry, BarRowCentersOnTheBarWhenFree)
{
    const BarRowRects row = BarRowLayout(0.0f, 1000.0f, 100.0f, 200.0f, 150.0f, 8.0f);
    EXPECT_EQ(row.Fit, BarRowFit::Centered);
    // The center block straddles the channel's midpoint, not the space left
    // over between the outer blocks.
    EXPECT_FLOAT_EQ(row.CenterMin, 400.0f);
    EXPECT_FLOAT_EQ(row.CenterMax, 600.0f);
    EXPECT_FLOAT_EQ((row.CenterMin + row.CenterMax) * 0.5f, 500.0f);
    EXPECT_FLOAT_EQ(row.LeftMin, 0.0f);
    EXPECT_FLOAT_EQ(row.LeftMax, 100.0f);
    EXPECT_FLOAT_EQ(row.RightMin, 850.0f);
    EXPECT_FLOAT_EQ(row.RightMax, 1000.0f);
}

TEST(ChromeGeometry, BarRowShiftsTheCenterOnlyAsFarAsTheOuterBlocksForce)
{
    // A wide left block pushes the center right, and no further than clearing it.
    const BarRowRects fromLeft = BarRowLayout(0.0f, 1000.0f, 500.0f, 200.0f, 150.0f, 8.0f);
    EXPECT_EQ(fromLeft.Fit, BarRowFit::Shifted);
    EXPECT_FLOAT_EQ(fromLeft.CenterMin, 508.0f);
    EXPECT_FLOAT_EQ(fromLeft.CenterMin, fromLeft.LeftMax + 8.0f);

    // A wide right block pushes it left, symmetrically.
    const BarRowRects fromRight = BarRowLayout(0.0f, 1000.0f, 100.0f, 200.0f, 500.0f, 8.0f);
    EXPECT_EQ(fromRight.Fit, BarRowFit::Shifted);
    EXPECT_FLOAT_EQ(fromRight.CenterMax, fromRight.RightMin - 8.0f);
    EXPECT_FLOAT_EQ(fromRight.CenterMin, 292.0f);

    // An outer block that does not reach the centered position leaves it alone.
    const BarRowRects free = BarRowLayout(0.0f, 1000.0f, 390.0f, 200.0f, 150.0f, 8.0f);
    EXPECT_EQ(free.Fit, BarRowFit::Centered);
    EXPECT_FLOAT_EQ(free.CenterMin, 400.0f);
}

TEST(ChromeGeometry, BarRowFlowsWhenTheBlocksCannotFit)
{
    const BarRowRects row = BarRowLayout(0.0f, 400.0f, 200.0f, 200.0f, 150.0f, 8.0f);
    EXPECT_EQ(row.Fit, BarRowFit::Flowed);
    // The center starts after the left block and overlaps the right one, but
    // both outer blocks keep their ends: whatever else a too-narrow bar does,
    // the controls at its right stay where a user reaches for them.
    EXPECT_FLOAT_EQ(row.CenterMin, 208.0f);
    EXPECT_FLOAT_EQ(row.CenterMax, 408.0f);
    EXPECT_FLOAT_EQ(row.LeftMin, 0.0f);
    EXPECT_FLOAT_EQ(row.LeftMax, 200.0f);
    EXPECT_FLOAT_EQ(row.RightMin, 250.0f);
    EXPECT_FLOAT_EQ(row.RightMax, 400.0f);
    EXPECT_GT(row.CenterMax, row.RightMin);
}

TEST(ChromeGeometry, BarRowFreeStripsBracketTheCenter)
{
    const BarRowRects row = BarRowLayout(0.0f, 1000.0f, 100.0f, 200.0f, 150.0f, 8.0f);
    EXPECT_FLOAT_EQ(row.LeftFreeMin, row.LeftMax);
    EXPECT_FLOAT_EQ(row.LeftFreeMax, row.CenterMin);
    EXPECT_FLOAT_EQ(row.RightFreeMin, row.CenterMax);
    EXPECT_FLOAT_EQ(row.RightFreeMax, row.RightMin);

    // Shifted hard against a block, the strip is the separation the row
    // mandates and no more: still a run, just too narrow to carry anything.
    const BarRowRects tight = BarRowLayout(0.0f, 1000.0f, 492.0f, 200.0f, 300.0f, 8.0f);
    EXPECT_FLOAT_EQ(tight.LeftFreeMax - tight.LeftFreeMin, 8.0f);

    // With no block on that side to separate from, the strip collapses rather
    // than inverting.
    const BarRowRects noLeft = BarRowLayout(0.0f, 1000.0f, 0.0f, 200.0f, 950.0f, 8.0f);
    EXPECT_FLOAT_EQ(noLeft.LeftFreeMin, noLeft.LeftFreeMax);
    const BarRowRects noRight = BarRowLayout(0.0f, 100.0f, 60.0f, 200.0f, 0.0f, 8.0f);
    EXPECT_FLOAT_EQ(noRight.RightFreeMin, noRight.RightFreeMax);
}

TEST(ChromeGeometry, WideHeaderHasEveryRegionAndTheLineFillsTheRest)
{
    const HeaderRegions r = LayoutHeader(ImVec2(0, 0), ImVec2(400, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_TRUE(r.HasCap);
    EXPECT_TRUE(r.HasTitle);
    EXPECT_TRUE(r.HasLine);
    EXPECT_TRUE(r.HasOrnament);
    EXPECT_TRUE(r.HasControl);
    EXPECT_FLOAT_EQ(r.CapMin.x, 0); EXPECT_FLOAT_EQ(r.CapMax.x, 12);
    EXPECT_FLOAT_EQ(r.TitleMin.x, 16); EXPECT_FLOAT_EQ(r.TitleMax.x, 96);
    EXPECT_FLOAT_EQ(r.ControlMin.x, 380); EXPECT_FLOAT_EQ(r.ControlMax.x, 400);
    EXPECT_FLOAT_EQ(r.OrnamentMax.x, 376); EXPECT_FLOAT_EQ(r.OrnamentMin.x, 346);
    EXPECT_FLOAT_EQ(r.LineMin.x, 100); EXPECT_FLOAT_EQ(r.LineMax.x, 342);
}

TEST(ChromeGeometry, NarrowHeaderYieldsLineThenOrnamentThenTitleThenCap)
{
    // Room for control + cap + title, no ornament, no line.
    HeaderRegions r = LayoutHeader(ImVec2(0, 0), ImVec2(120, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_TRUE(r.HasControl);
    EXPECT_TRUE(r.HasCap);
    EXPECT_TRUE(r.HasTitle);
    EXPECT_FALSE(r.HasOrnament);
    EXPECT_FALSE(r.HasLine);

    // Room for control + cap and a stub of line (two gaps is the shortest
    // line worth drawing).
    r = LayoutHeader(ImVec2(0, 0), ImVec2(48, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_TRUE(r.HasControl);
    EXPECT_TRUE(r.HasCap);
    EXPECT_FALSE(r.HasTitle);
    EXPECT_TRUE(r.HasLine);
    EXPECT_FLOAT_EQ(r.LineMax.x - r.LineMin.x, 8.0f);

    // Room for control + cap only.
    r = LayoutHeader(ImVec2(0, 0), ImVec2(44, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_TRUE(r.HasControl);
    EXPECT_TRUE(r.HasCap);
    EXPECT_FALSE(r.HasTitle);
    EXPECT_FALSE(r.HasLine);

    // The control region is kept before the cap.
    r = LayoutHeader(ImVec2(0, 0), ImVec2(22, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_TRUE(r.HasControl);
    EXPECT_FALSE(r.HasCap);

    // Nothing fits in nothing.
    r = LayoutHeader(ImVec2(0, 0), ImVec2(0, 20), 12.0f, 80.0f, 30.0f, 20.0f, 4.0f);
    EXPECT_FALSE(r.HasControl);
    EXPECT_FALSE(r.HasCap);
}

TEST(ChromeGeometry, HeaderWithoutTitleOrControlIsCapAndLine)
{
    const HeaderRegions r = LayoutHeader(ImVec2(10, 0), ImVec2(210, 6), 18.0f, 0.0f, 0.0f, 0.0f, 4.0f);
    EXPECT_TRUE(r.HasCap);
    EXPECT_FALSE(r.HasTitle);
    EXPECT_FALSE(r.HasControl);
    EXPECT_TRUE(r.HasLine);
    EXPECT_FLOAT_EQ(r.LineMin.x, 32);
    EXPECT_FLOAT_EQ(r.LineMax.x, 210);
}

TEST(ChromeGeometry, TierIsJudgedOnTheShorterSide)
{
    EXPECT_EQ(TierFor(ImVec2(1000, 100), 160.0f, 320.0f), OrnamentTier::Small);
    EXPECT_EQ(TierFor(ImVec2(159, 900), 160.0f, 320.0f), OrnamentTier::Small);
    EXPECT_EQ(TierFor(ImVec2(160, 900), 160.0f, 320.0f), OrnamentTier::Medium);
    EXPECT_EQ(TierFor(ImVec2(319, 900), 160.0f, 320.0f), OrnamentTier::Medium);
    EXPECT_EQ(TierFor(ImVec2(320, 320), 160.0f, 320.0f), OrnamentTier::Large);
}

TEST(ChromeGeometry, OrnamentsScaleWithTierAndStayInsideTheFrame)
{
    const FrameSpec spec{ .Chamfer = 6, .Border = 1, .Recess = 3, .Rail = 6 };
    const ImVec2 mn(0, 0);
    const ImVec2 mx(400, 400);
    const FrameRects rects = FrameLayout(mn, mx, spec);
    std::array<OrnamentSlot, 12> slots{};

    EXPECT_EQ(LayoutOrnaments(rects, OrnamentTier::Small, 3.0f, 24.0f, 12.0f, 4.0f, slots), 0);

    const int medium = LayoutOrnaments(rects, OrnamentTier::Medium, 3.0f, 24.0f, 12.0f, 4.0f, slots);
    ASSERT_EQ(medium, 3);
    EXPECT_EQ(CountKind(std::span(slots).first(medium), OrnamentKind::Screw), 2);
    EXPECT_EQ(CountKind(std::span(slots).first(medium), OrnamentKind::StatusLed), 1);
    for (int i = 0; i < medium; ++i)
    {
        EXPECT_TRUE(Inside(slots[i], rects.WellMin, rects.WellMax));
        if (slots[i].Kind == OrnamentKind::Screw)
        {
            // Bottom corners: below the rail, never over it.
            EXPECT_GT(slots[i].Min.y, rects.RailMax.y);
        }
        else
        {
            // The LED sits at the rail's right end, one gap in.
            EXPECT_TRUE(Inside(slots[i], rects.RailMin, rects.RailMax));
            EXPECT_FLOAT_EQ(slots[i].Max.x, rects.RailMax.x - 4.0f);
            EXPECT_FLOAT_EQ(slots[i].Max.x - slots[i].Min.x, 6.0f);
        }
    }

    const int large = LayoutOrnaments(rects, OrnamentTier::Large, 3.0f, 24.0f, 12.0f, 4.0f, slots);
    ASSERT_EQ(large, 5);
    const std::span<const OrnamentSlot> placed(slots.data(), static_cast<std::size_t>(large));
    EXPECT_EQ(CountKind(placed, OrnamentKind::Screw), 2);
    EXPECT_EQ(CountKind(placed, OrnamentKind::Vent), 1);
    EXPECT_EQ(CountKind(placed, OrnamentKind::TripleSlash), 1);
    EXPECT_EQ(CountKind(placed, OrnamentKind::StatusLed), 1);
    for (int i = 0; i < large; ++i)
    {
        EXPECT_TRUE(Inside(placed[i], mn, mx));
        for (int j = i + 1; j < large; ++j)
        {
            EXPECT_FALSE(Overlaps(placed[i], placed[j])) << i << " overlaps " << j;
        }
        if (placed[i].Kind != OrnamentKind::Screw)
        {
            EXPECT_TRUE(Inside(placed[i], rects.RailMin, rects.RailMax));
        }
    }
    // The rail ornaments end at the rail's right edge minus the gap, in the
    // width the rail's line was told to leave free.
    EXPECT_FLOAT_EQ(RailOrnamentWidth(OrnamentTier::Large, 24.0f, 12.0f, 6.0f, 4.0f), 54.0f);
    EXPECT_FLOAT_EQ(RailOrnamentWidth(OrnamentTier::Medium, 24.0f, 12.0f, 6.0f, 4.0f), 10.0f);
    EXPECT_FLOAT_EQ(RailOrnamentWidth(OrnamentTier::Small, 24.0f, 12.0f, 6.0f, 4.0f), 0.0f);
}

TEST(ChromeGeometry, OrnamentsRespectTheOutputCapacityAndSmallFrames)
{
    const FrameSpec spec{ .Chamfer = 6, .Border = 1, .Recess = 3, .Rail = 6 };
    std::array<OrnamentSlot, 1> one{};
    EXPECT_EQ(LayoutOrnaments(FrameLayout(ImVec2(0, 0), ImVec2(400, 400), spec), OrnamentTier::Large,
                              3.0f, 24.0f, 12.0f, 4.0f, one), 1);

    // A well too small for two screws gets none, and a rail too short for a
    // cap, a line, and the ornaments carries no ornaments.
    std::array<OrnamentSlot, 12> slots{};
    EXPECT_EQ(LayoutOrnaments(FrameLayout(ImVec2(0, 0), ImVec2(20, 20), spec), OrnamentTier::Medium,
                              3.0f, 24.0f, 12.0f, 4.0f, slots), 0);
    // A rail too short for the vent and slash keeps its LED.
    const int narrow = LayoutOrnaments(FrameLayout(ImVec2(0, 0), ImVec2(70, 400), spec), OrnamentTier::Large,
                                       3.0f, 24.0f, 12.0f, 4.0f, slots);
    EXPECT_EQ(CountKind(std::span(slots).first(narrow), OrnamentKind::Vent), 0);
    EXPECT_EQ(CountKind(std::span(slots).first(narrow), OrnamentKind::StatusLed), 1);
    // And one too short for the LED as well carries nothing on the rail.
    const int tiny = LayoutOrnaments(FrameLayout(ImVec2(0, 0), ImVec2(40, 400), spec), OrnamentTier::Large,
                                     3.0f, 24.0f, 12.0f, 4.0f, slots);
    EXPECT_EQ(CountKind(std::span(slots).first(tiny), OrnamentKind::StatusLed), 0);
}

TEST(ChromeGeometry, CornerWedgeHugsTheChamferAndClampsToTheFrame)
{
    const ChamferPoly wedge = CornerWedge(ImVec2(10, 20), ImVec2(210, 120), 8.0f, 2.0f);
    ASSERT_EQ(wedge.Count, 4);
    // From the left edge up along the cut, along the top, then back down the
    // left edge, thickness * sqrt(2) further along each edge.
    EXPECT_FLOAT_EQ(wedge.P[0].x, 10.0f);
    EXPECT_FLOAT_EQ(wedge.P[0].y, 28.0f);
    EXPECT_FLOAT_EQ(wedge.P[1].x, 18.0f);
    EXPECT_FLOAT_EQ(wedge.P[1].y, 20.0f);
    EXPECT_NEAR(wedge.P[2].x, 10.0f + 8.0f + 2.0f * 1.41421356f, 1e-4f);
    EXPECT_FLOAT_EQ(wedge.P[2].y, 20.0f);
    EXPECT_FLOAT_EQ(wedge.P[3].x, 10.0f);
    EXPECT_NEAR(wedge.P[3].y, 20.0f + 8.0f + 2.0f * 1.41421356f, 1e-4f);

    EXPECT_EQ(CornerWedge(ImVec2(0, 0), ImVec2(100, 100), 0.0f, 2.0f).Count, 0);
    EXPECT_EQ(CornerWedge(ImVec2(0, 0), ImVec2(100, 0), 8.0f, 2.0f).Count, 0);
    // On a frame smaller than the band the reach stops at half the side.
    const ChamferPoly tiny = CornerWedge(ImVec2(0, 0), ImVec2(12, 12), 5.0f, 4.0f);
    ASSERT_EQ(tiny.Count, 4);
    EXPECT_FLOAT_EQ(tiny.P[2].x, 6.0f);
    EXPECT_FLOAT_EQ(tiny.P[3].y, 6.0f);
    EXPECT_EQ(CornerWedge(ImVec2(0, 0), ImVec2(12, 12), 6.0f, 4.0f).Count, 0);
}

TEST(ChromeGeometry, SlantedCapLeansRightAndClampsTheLean)
{
    const ChamferPoly cap = SlantedCap(ImVec2(0, 0), ImVec2(20, 10), 3.0f);
    ASSERT_EQ(cap.Count, 4);
    EXPECT_FLOAT_EQ(cap.P[0].x, 3.0f);
    EXPECT_FLOAT_EQ(cap.P[0].y, 0.0f);
    EXPECT_FLOAT_EQ(cap.P[1].x, 20.0f);
    EXPECT_FLOAT_EQ(cap.P[1].y, 0.0f);
    EXPECT_FLOAT_EQ(cap.P[2].x, 17.0f);
    EXPECT_FLOAT_EQ(cap.P[2].y, 10.0f);
    EXPECT_FLOAT_EQ(cap.P[3].x, 0.0f);
    EXPECT_FLOAT_EQ(cap.P[3].y, 10.0f);
    EXPECT_FLOAT_EQ(SlantedCap(ImVec2(0, 0), ImVec2(20, 10), 50.0f).P[0].x, 10.0f);
    EXPECT_EQ(SlantedCap(ImVec2(0, 0), ImVec2(0, 10), 3.0f).Count, 0);
}

TEST(ChromeGeometry, RingOrnamentsStayOnTheRingAndOutOfTheWell)
{
    const ImVec2 mn(0.0f, 0.0f);
    const ImVec2 mx(600.0f, 400.0f);
    const float ring = 9.0f;
    std::array<OrnamentSlot, 8> slots{};
    const int placed = LayoutRingOrnaments(mn, mx, ring, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, slots);

    // Four bolts, two strips, one vent on a frame with room for all of it.
    EXPECT_EQ(placed, 7);
    const std::span<const OrnamentSlot> used(slots.data(), static_cast<std::size_t>(placed));
    EXPECT_EQ(CountKind(used, OrnamentKind::Bolt), 4);
    EXPECT_EQ(CountKind(used, OrnamentKind::LightStrip), 2);
    EXPECT_EQ(CountKind(used, OrnamentKind::Vent), 1);

    // The point of mounting on the ring: the body covers the well, so nothing
    // may land inside it.
    const ImVec2 wellMin(mn.x + ring, mn.y + ring);
    const ImVec2 wellMax(mx.x - ring, mx.y - ring);
    for (const OrnamentSlot& slot : used)
    {
        EXPECT_GE(slot.Min.x, mn.x);
        EXPECT_LE(slot.Max.x, mx.x);
        EXPECT_GE(slot.Min.y, mn.y);
        EXPECT_LE(slot.Max.y, mx.y);
        const bool insideWell = slot.Min.x > wellMin.x && slot.Max.x < wellMax.x
                             && slot.Min.y > wellMin.y && slot.Max.y < wellMax.y;
        EXPECT_FALSE(insideWell);
    }
}

TEST(ChromeGeometry, RingOrnamentsYieldTheVentAndStripsBeforeTheBolts)
{
    std::array<OrnamentSlot, 8> slots{};
    // Wide enough for everything, then progressively narrower.
    EXPECT_EQ(LayoutRingOrnaments(ImVec2(0, 0), ImVec2(600, 400), 9.0f, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, slots), 7);
    // Narrow enough that a pair of strips no longer fits the run, but a vent
    // still does: the bolts and the vent survive.
    const int narrow = LayoutRingOrnaments(ImVec2(0, 0), ImVec2(95, 120), 9.0f, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, slots);
    EXPECT_EQ(narrow, 5);
    EXPECT_EQ(CountKind(std::span<const OrnamentSlot>(slots.data(), static_cast<std::size_t>(narrow)),
                        OrnamentKind::Bolt), 4);
    // Smaller than four chamfers: no ring worth mounting anything on.
    EXPECT_EQ(LayoutRingOrnaments(ImVec2(0, 0), ImVec2(40, 40), 9.0f, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, slots), 0);
    // A ring too thin to hold a bolt places nothing either.
    EXPECT_EQ(LayoutRingOrnaments(ImVec2(0, 0), ImVec2(600, 400), 1.0f, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, slots), 0);
    // The caller's capacity is honored.
    std::array<OrnamentSlot, 2> few{};
    EXPECT_EQ(LayoutRingOrnaments(ImVec2(0, 0), ImVec2(600, 400), 9.0f, 14.0f, 4.0f, 24.0f, 14.0f, 4.0f, few), 2);
}

TEST(ChromeGeometry, SurfaceTileUvCountsHowOftenTheArtFits)
{
    // A strip a third of the width repeats three times; one exactly as wide
    // repeats once. Only true against a repeating sampler.
    EXPECT_FLOAT_EQ(SurfaceTileUv(ImVec2(300.0f, 33.0f), ImVec2(100.0f, 33.0f)).x, 3.0f);
    EXPECT_FLOAT_EQ(SurfaceTileUv(ImVec2(300.0f, 33.0f), ImVec2(100.0f, 33.0f)).y, 1.0f);
    EXPECT_FLOAT_EQ(SurfaceTileUv(ImVec2(100.0f, 33.0f), ImVec2(100.0f, 33.0f)).x, 1.0f);
    // A degenerate texture tiles once rather than dividing by zero.
    EXPECT_FLOAT_EQ(SurfaceTileUv(ImVec2(300.0f, 33.0f), ImVec2(0.0f, 0.0f)).x, 1.0f);
}

TEST(ChromeGeometry, TileFootprintAndBadgeStayInsideFace)
{
    const auto tile = TileLayout(ImVec2(10, 20), 96, 15, 15, 4);
    EXPECT_FLOAT_EQ(tile.Size.y, 111);
    EXPECT_FLOAT_EQ(tile.LabelMin.y, tile.FaceMax.y);
    EXPECT_GE(tile.BadgeMin.x, tile.FaceMin.x);
    EXPECT_GE(tile.BadgeMin.y, tile.FaceMin.y);
    EXPECT_LE(tile.BadgeMax.x, tile.FaceMax.x);
    EXPECT_LE(tile.BadgeMax.y, tile.FaceMax.y);
    const auto tiny = TileLayout(ImVec2(), 2, 0, 15, 4);
    EXPECT_FLOAT_EQ(tiny.Size.y, 2);
    EXPECT_LE(tiny.BadgeMax.x, tiny.FaceMax.x);
    EXPECT_GE(tiny.BadgeMin.y, tiny.FaceMin.y);
}

TEST(ChromeGeometry, CornerBracketsClampAndRejectDegenerateRects)
{
    const auto brackets = BracketCorners(ImVec2(10, 20), ImVec2(14, 22), 20);
    EXPECT_EQ(brackets.Count, 16);
    for (int i = 0; i < brackets.Count; ++i)
    {
        EXPECT_GE(brackets.Points[i].x, 10);
        EXPECT_LE(brackets.Points[i].x, 14);
        EXPECT_GE(brackets.Points[i].y, 20);
        EXPECT_LE(brackets.Points[i].y, 22);
    }
    EXPECT_EQ(BracketCorners(ImVec2(), ImVec2(), 10).Count, 0);
    EXPECT_EQ(BracketCorners(ImVec2(), ImVec2(10, 10), 0).Count, 0);
}

TEST(ChromeGeometry, ReadoutReservesOnlyRequestedLedAndTextWidths)
{
    const auto plain = ReadoutLayout(ImVec2(10, 20), 24, 48, 24, 4, 5, 0);
    EXPECT_FALSE(plain.HasLed);
    EXPECT_FLOAT_EQ(plain.Size.x, 85);
    EXPECT_FLOAT_EQ(plain.LabelMin.x, 14);
    const auto lit = ReadoutLayout(ImVec2(10, 20), 24, 48, 24, 4, 5, 6);
    EXPECT_TRUE(lit.HasLed);
    EXPECT_FLOAT_EQ(lit.Size.x, plain.Size.x + 11);
    EXPECT_FLOAT_EQ(lit.LedMin.y, 29);
    EXPECT_LT(lit.LedMax.x, lit.LabelMin.x);
}
