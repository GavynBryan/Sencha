#include "tools/ToolWheelMath.h"

#include <gtest/gtest.h>

#include <numbers>

// The wheel's geometry is one rule for the session that hit-tests it and the
// chrome that paints it: sector 0 straight up, clockwise, the hub selecting
// nothing, and a layout resolved once in physical pixels that stays inside
// the frame it was opened in.
namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

ToolWheel::Frame Frame(float scale = 1.0f, ImVec2 mn = { 0.0f, 0.0f }, ImVec2 mx = { 1600.0f, 900.0f })
{
    return { .Scale = scale, .Min = mn, .Max = mx };
}
}

TEST(ToolWheelMath, SectorZeroIsStraightUpAndSectorsProceedClockwise)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 4);
    const ImVec2 c = layout.Center;
    EXPECT_EQ(ToolWheel::SectorAt(layout, { c.x, c.y - 100.0f }), 0);
    EXPECT_EQ(ToolWheel::SectorAt(layout, { c.x + 100.0f, c.y }), 1);
    EXPECT_EQ(ToolWheel::SectorAt(layout, { c.x, c.y + 100.0f }), 2);
    EXPECT_EQ(ToolWheel::SectorAt(layout, { c.x - 100.0f, c.y }), 3);
    // Slightly anticlockwise of straight up is still sector 0: it is centred
    // there, not begun there.
    EXPECT_EQ(ToolWheel::SectorAt(layout, { c.x - 10.0f, c.y - 100.0f }), 0);
}

TEST(ToolWheelMath, SpansTileTheCircleWithoutGaps)
{
    for (const int count : { 1, 2, 5, 7 })
    {
        float expectedBegin = -kPi / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
        {
            const ToolWheel::Span span = ToolWheel::SectorSpan(i, count);
            EXPECT_NEAR(span.Begin, expectedBegin, 1e-5f) << count << " sectors, sector " << i;
            expectedBegin = span.End;
        }
        EXPECT_NEAR(expectedBegin, 2.0f * kPi - kPi / static_cast<float>(count), 1e-4f);
    }
}

TEST(ToolWheelMath, TheHubSelectsNothing)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 5);
    EXPECT_EQ(ToolWheel::SectorAt(layout, layout.Center), -1);
    EXPECT_EQ(ToolWheel::SectorAt(layout, { layout.Center.x, layout.Center.y - layout.Hub + 1.0f }), -1);
    EXPECT_EQ(ToolWheel::SectorAt(layout, { layout.Center.x, layout.Center.y - layout.Hub - 1.0f }), 0);
    // Direction alone decides beyond the hub: the wedge reaches the screen edge.
    EXPECT_EQ(ToolWheel::SectorAt(layout, { layout.Center.x, layout.Center.y - 5000.0f }), 0);
    EXPECT_EQ(ToolWheel::SectorAt(ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 0), { 800.0f, 0.0f }), -1);
}

TEST(ToolWheelMath, SlotCentresLieOnTheRingAtTheSectorsMiddle)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 4);
    const ImVec2 top = ToolWheel::SlotCenter(layout, 0);
    EXPECT_NEAR(top.x, layout.Center.x, 1e-3f);
    EXPECT_NEAR(top.y, layout.Center.y - layout.Radius, 1e-3f);
    const ImVec2 right = ToolWheel::SlotCenter(layout, 1);
    EXPECT_NEAR(right.x, layout.Center.x + layout.Radius, 1e-3f);
    EXPECT_NEAR(right.y, layout.Center.y, 1e-3f);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(ToolWheel::SectorAt(layout, ToolWheel::SlotCenter(layout, i)), i);
}

TEST(ToolWheelMath, PlaceScalesEveryFieldExactlyOnce)
{
    const ToolWheel::Layout one = ToolWheel::Place(Frame(1.0f), { 800.0f, 450.0f }, 3);
    const ToolWheel::Layout two = ToolWheel::Place(Frame(2.0f), { 800.0f, 450.0f }, 3);
    EXPECT_FLOAT_EQ(one.Radius, ToolWheel::kRadius);
    EXPECT_FLOAT_EQ(two.Radius, ToolWheel::kRadius * 2.0f);
    EXPECT_FLOAT_EQ(two.Hub, one.Hub * 2.0f);
    EXPECT_FLOAT_EQ(two.Button, one.Button * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionGap, one.CaptionGap * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionHeight, one.CaptionHeight * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionY() - two.Center.y, (one.CaptionY() - one.Center.y) * 2.0f);
}

TEST(ToolWheelMath, AWheelOpenedAtTheEdgeIsShiftedInsideNotClipped)
{
    const ToolWheel::Frame frame = Frame();
    const ToolWheel::Layout left = ToolWheel::Place(frame, { 10.0f, 450.0f }, 5);
    EXPECT_FLOAT_EQ(left.Center.x, frame.Min.x + left.HalfWidth());
    EXPECT_FLOAT_EQ(left.Center.y, 450.0f);
    // Every sector is reachable from where the pointer actually is.
    EXPECT_EQ(ToolWheel::SectorAt(left, { 10.0f, 450.0f }), 4); // the pointer itself now lies to the wheel's left

    const ToolWheel::Layout corner = ToolWheel::Place(frame, { 1595.0f, 895.0f }, 5);
    EXPECT_FLOAT_EQ(corner.Center.x, frame.Max.x - corner.HalfWidth());
    EXPECT_FLOAT_EQ(corner.Center.y, frame.Max.y - corner.HalfHeightBelow());
    EXPECT_GE(corner.CaptionY() + corner.CaptionHeight, corner.Center.y);
    EXPECT_LE(corner.CaptionY() + corner.CaptionHeight, frame.Max.y);

    const ToolWheel::Layout top = ToolWheel::Place(frame, { 800.0f, 3.0f }, 5);
    EXPECT_FLOAT_EQ(top.Center.y, frame.Min.y + top.HalfHeightAbove());

    // A work area offset from the window origin is honoured, not assumed zero.
    const ToolWheel::Layout offset = ToolWheel::Place(Frame(1.0f, { 100.0f, 50.0f }, { 1700.0f, 950.0f }), { 105.0f, 55.0f }, 5);
    EXPECT_FLOAT_EQ(offset.Center.x, 100.0f + offset.HalfWidth());
    EXPECT_FLOAT_EQ(offset.Center.y, 50.0f + offset.HalfHeightAbove());
}

TEST(ToolWheelMath, AFrameTooSmallCentresTheWheelOnThatAxis)
{
    const ToolWheel::Layout tiny = ToolWheel::Place(Frame(1.0f, { 0.0f, 0.0f }, { 100.0f, 2000.0f }), { 90.0f, 300.0f }, 5);
    EXPECT_FLOAT_EQ(tiny.Center.x, 50.0f);
    EXPECT_FLOAT_EQ(tiny.Center.y, 300.0f);
}

// The variant ring: a hot tool's choices laid outside the rim as that tool's
// sector continued outward and split evenly, so they read as its children
// and every point in the parent's direction is in its fan.
TEST(ToolWheelMath, VariantsSplitTheParentsSectorEvenlyAndEndOnItsFlanks)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    ASSERT_GT(layout.OuterRadius, layout.RimOuter());
    for (const int count : { 3, 4 })
    {
        const ToolWheel::Span parent = ToolWheel::SectorSpan(1, 5);
        float expectedBegin = parent.Begin;
        for (int i = 0; i < count; ++i)
        {
            const ToolWheel::Span span = ToolWheel::VariantSpan(layout, 1, i, count);
            EXPECT_NEAR(span.Begin, expectedBegin, 1e-5f) << count << " variants, variant " << i;
            EXPECT_NEAR(span.End - span.Begin, (parent.End - parent.Begin) / static_cast<float>(count), 1e-5f);
            expectedBegin = span.End;
        }
        EXPECT_NEAR(expectedBegin, parent.End, 1e-4f);
    }
}

TEST(ToolWheelMath, ADensePrimaryRingWidensTheFanToTheArcFloorButKeepsItCentred)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 12, 4);
    const ToolWheel::Span parent = ToolWheel::SectorSpan(3, 12);
    const float parentMiddle = (parent.Begin + parent.End) * 0.5f;
    const ToolWheel::Span first = ToolWheel::VariantSpan(layout, 3, 0, 4);
    const ToolWheel::Span last = ToolWheel::VariantSpan(layout, 3, 3, 4);
    // Each petal has at least the arc floor at its ring, and more than the
    // even split of a 30 degree sector would give.
    EXPECT_GE((first.End - first.Begin) * layout.OuterRadius, layout.ChildArc - 1e-3f);
    EXPECT_GT(first.End - first.Begin, (parent.End - parent.Begin) / 4.0f);
    // The fan overhangs the parent symmetrically and still contains its sector.
    EXPECT_NEAR((first.Begin + last.End) * 0.5f, parentMiddle, 1e-5f);
    EXPECT_LE(first.Begin, parent.Begin + 1e-5f);
    EXPECT_GE(last.End, parent.End - 1e-5f);
    // Never past a full turn between the variants.
    const ToolWheel::Layout tiny = ToolWheel::Place(Frame(0.1f), { 800.0f, 450.0f }, 12, 40);
    EXPECT_LE(ToolWheel::VariantWidth(tiny, 40) * 40.0f, 2.0f * kPi + 1e-4f);
}

TEST(ToolWheelMath, RingsAreByRadiusAndTheOuterOneIsUnbounded)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    const ImVec2 c = layout.Center;
    EXPECT_EQ(ToolWheel::RingAt(layout, c), ToolWheel::Ring::Hub);
    EXPECT_EQ(ToolWheel::RingAt(layout, { c.x, c.y - layout.Hub + 1.0f }), ToolWheel::Ring::Hub);
    EXPECT_EQ(ToolWheel::RingAt(layout, { c.x, c.y - layout.Hub - 1.0f }), ToolWheel::Ring::Primary);
    EXPECT_EQ(ToolWheel::RingAt(layout, { c.x, c.y - layout.RimOuter() + 1.0f }), ToolWheel::Ring::Primary);
    EXPECT_EQ(ToolWheel::RingAt(layout, { c.x, c.y - layout.RimOuter() - 1.0f }), ToolWheel::Ring::Outer);
    EXPECT_EQ(ToolWheel::RingAt(layout, { c.x, c.y - 5000.0f }), ToolWheel::Ring::Outer);
    // Without variants there is no outer band: the primary wedge reaches the edge.
    const ToolWheel::Layout plain = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 5, 0);
    EXPECT_EQ(ToolWheel::RingAt(plain, { plain.Center.x, plain.Center.y - 5000.0f }), ToolWheel::Ring::Primary);
    EXPECT_FLOAT_EQ(plain.OuterRadius, 0.0f);
}

TEST(ToolWheelMath, VariantSlotsRoundTripAndTheFanCoversTheParentsSector)
{
    const ToolWheel::Layout layout = ToolWheel::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    for (int parent = 0; parent < 5; ++parent)
    {
        for (int i = 0; i < 4; ++i)
        {
            const ImVec2 slot = ToolWheel::VariantSlotCenter(layout, parent, i, 4);
            EXPECT_EQ(ToolWheel::VariantAt(layout, parent, 4, slot), i) << "parent " << parent << " variant " << i;
            EXPECT_EQ(ToolWheel::RingAt(layout, slot), ToolWheel::Ring::Outer);
            // The slot lies in its parent's direction.
            EXPECT_EQ(ToolWheel::SectorAt(layout, slot), parent);
        }
        // Every direction in the parent's sector is in its fan, and the
        // neighbour's direction is not.
        const ToolWheel::Span sector = ToolWheel::SectorSpan(parent, 5);
        for (const float t : { 0.02f, 0.5f, 0.98f })
        {
            const float angle = sector.Begin + (sector.End - sector.Begin) * t;
            const ImVec2 p{ layout.Center.x + std::sin(angle) * 300.0f, layout.Center.y - std::cos(angle) * 300.0f };
            EXPECT_GE(ToolWheel::VariantAt(layout, parent, 4, p), 0) << "parent " << parent << " t " << t;
        }
        const ImVec2 next = ToolWheel::SlotCenter(layout, (parent + 1) % 5);
        EXPECT_EQ(ToolWheel::VariantAt(layout, parent, 4, next), -1);
    }
    EXPECT_EQ(ToolWheel::VariantAt(layout, 0, 0, layout.Center), -1);
    EXPECT_EQ(ToolWheel::VariantAt(layout, -1, 4, layout.Center), -1);
}

TEST(ToolWheelMath, PlaceReservesTheOuterRingOnlyWhenAToolHasVariants)
{
    const ToolWheel::Frame frame = Frame();
    const ToolWheel::Layout plain = ToolWheel::Place(frame, { 5.0f, 5.0f }, 5, 0);
    const ToolWheel::Layout ringed = ToolWheel::Place(frame, { 5.0f, 5.0f }, 5, 4);
    EXPECT_FLOAT_EQ(plain.Center.x, frame.Min.x + plain.RimOuter());
    EXPECT_FLOAT_EQ(ringed.Center.x, frame.Min.x + ringed.OuterExtent());
    EXPECT_GT(ringed.OuterExtent(), plain.OuterExtent());
    EXPECT_FLOAT_EQ(plain.OuterExtent(), plain.RimOuter());
    EXPECT_FLOAT_EQ(ringed.Center.y, frame.Min.y + ringed.OuterExtent());

    const ToolWheel::Layout corner = ToolWheel::Place(frame, { 1595.0f, 895.0f }, 5, 4);
    EXPECT_LE(corner.Center.x + corner.OuterExtent(), frame.Max.x);
    EXPECT_LE(corner.CaptionY() + corner.CaptionHeight, frame.Max.y);
    EXPECT_GE(corner.CaptionY(), corner.Center.y + corner.OuterExtent());

    // Scaled once, like every other field.
    const ToolWheel::Layout one = ToolWheel::Place(Frame(1.0f), { 800.0f, 450.0f }, 5, 4);
    const ToolWheel::Layout two = ToolWheel::Place(Frame(2.0f), { 800.0f, 450.0f }, 5, 4);
    EXPECT_FLOAT_EQ(two.Seam, one.Seam * 2.0f);
    EXPECT_FLOAT_EQ(two.Rim, one.Rim * 2.0f);
    EXPECT_FLOAT_EQ(two.ChildArc, one.ChildArc * 2.0f);
    EXPECT_FLOAT_EQ(two.OuterRadius, one.OuterRadius * 2.0f);
}
