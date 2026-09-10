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
