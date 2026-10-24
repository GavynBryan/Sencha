#include "tools/RadialMenuMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

// The wheel's geometry is one rule for the session that hit-tests it and the
// chrome that paints it: sector 0 straight up, clockwise, the hub selecting
// nothing, and a layout resolved once in physical pixels that stays inside
// the frame it was opened in.
namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

RadialMenu::Frame Frame(float scale = 1.0f, Vec2d mn = { 0.0f, 0.0f }, Vec2d mx = { 1600.0f, 900.0f })
{
    return { .Scale = scale, .Min = mn, .Max = mx };
}
}

TEST(RadialMenuMath, SectorZeroIsStraightUpAndSectorsProceedClockwise)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 4);
    const Vec2d c = layout.Center;
    EXPECT_EQ(RadialMenu::SectorAt(layout, { c.X, c.Y - 100.0f }), 0);
    EXPECT_EQ(RadialMenu::SectorAt(layout, { c.X + 100.0f, c.Y }), 1);
    EXPECT_EQ(RadialMenu::SectorAt(layout, { c.X, c.Y + 100.0f }), 2);
    EXPECT_EQ(RadialMenu::SectorAt(layout, { c.X - 100.0f, c.Y }), 3);
    // Slightly anticlockwise of straight up is still sector 0: it is centred
    // there, not begun there.
    EXPECT_EQ(RadialMenu::SectorAt(layout, { c.X - 10.0f, c.Y - 100.0f }), 0);
}

TEST(RadialMenuMath, SpansTileTheCircleWithoutGaps)
{
    for (const int count : { 1, 2, 5, 7 })
    {
        float expectedBegin = -kPi / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
        {
            const RadialMenu::Span span = RadialMenu::SectorSpan(i, count);
            EXPECT_NEAR(span.Begin, expectedBegin, 1e-5f) << count << " sectors, sector " << i;
            expectedBegin = span.End;
        }
        EXPECT_NEAR(expectedBegin, 2.0f * kPi - kPi / static_cast<float>(count), 1e-4f);
    }
}

TEST(RadialMenuMath, TheHubSelectsNothing)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 5);
    EXPECT_EQ(RadialMenu::SectorAt(layout, layout.Center), -1);
    EXPECT_EQ(RadialMenu::SectorAt(layout, { layout.Center.X, layout.Center.Y - layout.Hub + 1.0f }), -1);
    EXPECT_EQ(RadialMenu::SectorAt(layout, { layout.Center.X, layout.Center.Y - layout.Hub - 1.0f }), 0);
    // Direction alone decides beyond the hub: the wedge reaches the screen edge.
    EXPECT_EQ(RadialMenu::SectorAt(layout, { layout.Center.X, layout.Center.Y - 5000.0f }), 0);
    EXPECT_EQ(RadialMenu::SectorAt(RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 0), { 800.0f, 0.0f }), -1);
}

TEST(RadialMenuMath, SlotCentresLieOnTheRingAtTheSectorsMiddle)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 4);
    const Vec2d top = RadialMenu::SlotCenter(layout, 0);
    EXPECT_NEAR(top.X, layout.Center.X, 1e-3f);
    EXPECT_NEAR(top.Y, layout.Center.Y - layout.Radius, 1e-3f);
    const Vec2d right = RadialMenu::SlotCenter(layout, 1);
    EXPECT_NEAR(right.X, layout.Center.X + layout.Radius, 1e-3f);
    EXPECT_NEAR(right.Y, layout.Center.Y, 1e-3f);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(RadialMenu::SectorAt(layout, RadialMenu::SlotCenter(layout, i)), i);
}

TEST(RadialMenuMath, PlaceScalesEveryFieldExactlyOnce)
{
    const RadialMenu::Layout one = RadialMenu::Place(Frame(1.0f), { 800.0f, 450.0f }, 3);
    const RadialMenu::Layout two = RadialMenu::Place(Frame(2.0f), { 800.0f, 450.0f }, 3);
    EXPECT_FLOAT_EQ(one.Radius, RadialMenu::kRadius);
    EXPECT_FLOAT_EQ(two.Radius, RadialMenu::kRadius * 2.0f);
    EXPECT_FLOAT_EQ(two.Hub, one.Hub * 2.0f);
    EXPECT_FLOAT_EQ(two.Button, one.Button * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionGap, one.CaptionGap * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionHeight, one.CaptionHeight * 2.0f);
    EXPECT_FLOAT_EQ(two.CaptionY() - two.Center.Y, (one.CaptionY() - one.Center.Y) * 2.0f);
}

TEST(RadialMenuMath, AWheelOpenedAtTheEdgeIsShiftedInsideNotClipped)
{
    const RadialMenu::Frame frame = Frame();
    const RadialMenu::Layout left = RadialMenu::Place(frame, { 10.0f, 450.0f }, 5);
    EXPECT_FLOAT_EQ(left.Center.X, frame.Min.X + left.HalfWidth());
    EXPECT_FLOAT_EQ(left.Center.Y, 450.0f);
    // Every sector is reachable from where the pointer actually is.
    EXPECT_EQ(RadialMenu::SectorAt(left, { 10.0f, 450.0f }), 4); // the pointer itself now lies to the wheel's left

    const RadialMenu::Layout corner = RadialMenu::Place(frame, { 1595.0f, 895.0f }, 5);
    EXPECT_FLOAT_EQ(corner.Center.X, frame.Max.X - corner.HalfWidth());
    EXPECT_FLOAT_EQ(corner.Center.Y, frame.Max.Y - corner.HalfHeightBelow());
    EXPECT_GE(corner.CaptionY() + corner.CaptionHeight, corner.Center.Y);
    EXPECT_LE(corner.CaptionY() + corner.CaptionHeight, frame.Max.Y);

    const RadialMenu::Layout top = RadialMenu::Place(frame, { 800.0f, 3.0f }, 5);
    EXPECT_FLOAT_EQ(top.Center.Y, frame.Min.Y + top.HalfHeightAbove());

    // A work area offset from the window origin is honoured, not assumed zero.
    const RadialMenu::Layout offset = RadialMenu::Place(Frame(1.0f, { 100.0f, 50.0f }, { 1700.0f, 950.0f }), { 105.0f, 55.0f }, 5);
    EXPECT_FLOAT_EQ(offset.Center.X, 100.0f + offset.HalfWidth());
    EXPECT_FLOAT_EQ(offset.Center.Y, 50.0f + offset.HalfHeightAbove());
}

TEST(RadialMenuMath, AFrameTooSmallCentresTheWheelOnThatAxis)
{
    const RadialMenu::Layout tiny = RadialMenu::Place(Frame(1.0f, { 0.0f, 0.0f }, { 100.0f, 2000.0f }), { 90.0f, 300.0f }, 5);
    EXPECT_FLOAT_EQ(tiny.Center.X, 50.0f);
    EXPECT_FLOAT_EQ(tiny.Center.Y, 300.0f);
}

// The variant fan: a hot tool's choices outside the rim, each a standard
// wedge, centred on the tool's own direction and no wider than they need,
// laid by the same slots-over-a-range rule as the primary ring.
TEST(RadialMenuMath, SlotsOverARangeAreEvenAndFoundByAngle)
{
    const RadialMenu::Range range{ 1.0f, 0.9f };
    float expectedBegin = range.Begin;
    for (int i = 0; i < 3; ++i)
    {
        const RadialMenu::Span span = RadialMenu::SlotSpan(range, i, 3);
        EXPECT_NEAR(span.Begin, expectedBegin, 1e-5f);
        EXPECT_NEAR(span.End - span.Begin, 0.3f, 1e-5f);
        EXPECT_EQ(RadialMenu::SlotAt(range, 3, (span.Begin + span.End) * 0.5f), i);
        expectedBegin = span.End;
    }
    EXPECT_EQ(RadialMenu::SlotAt(range, 3, range.Begin - 0.01f), -1);
    EXPECT_EQ(RadialMenu::SlotAt(range, 3, range.Begin + range.Width + 0.01f), -1);
    // A turn away is the same direction.
    EXPECT_EQ(RadialMenu::SlotAt(range, 3, 1.1f - 2.0f * kPi), 0);
    // The primary ring is the same rule over the full circle.
    for (int i = 0; i < 5; ++i)
    {
        const RadialMenu::Span sector = RadialMenu::SectorSpan(i, 5);
        const RadialMenu::Span slot = RadialMenu::SlotSpan(RadialMenu::PrimaryRange(5), i, 5);
        EXPECT_FLOAT_EQ(sector.Begin, slot.Begin);
        EXPECT_FLOAT_EQ(sector.End, slot.End);
    }
}

TEST(RadialMenuMath, AFanIsAStandardWedgePerVariantCentredOnItsParent)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    ASSERT_GT(layout.OuterRadius, layout.RimOuter());
    for (const int count : { 3, 4 })
    {
        for (const int parent : { 0, 1, 4 })
        {
            const RadialMenu::Span sector = RadialMenu::SectorSpan(parent, 5);
            const float middle = (sector.Begin + sector.End) * 0.5f;
            const RadialMenu::Range fan = RadialMenu::FanRange(layout, parent, count);
            EXPECT_NEAR(fan.Width, RadialMenu::kVariantWidth * static_cast<float>(count), 1e-5f) << count;
            EXPECT_NEAR(fan.Begin + fan.Width * 0.5f, middle, 1e-5f) << count << " parent " << parent;
            float expectedBegin = fan.Begin;
            for (int i = 0; i < count; ++i)
            {
                const RadialMenu::Span span = RadialMenu::VariantSpan(layout, parent, i, count);
                EXPECT_NEAR(span.Begin, expectedBegin, 1e-5f) << count << " variants, variant " << i;
                EXPECT_NEAR(span.End - span.Begin, RadialMenu::kVariantWidth, 1e-5f);
                expectedBegin = span.End;
            }
        }
    }
    // The wedge is standard whatever the primary ring's density: a fan of
    // three is the same three wedges over twelve tools as over five.
    const RadialMenu::Layout dense = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 12, 4);
    EXPECT_NEAR(RadialMenu::FanRange(dense, 3, 3).Width, RadialMenu::FanRange(layout, 1, 3).Width, 1e-5f);
    // Many variants share the turn rather than overlap.
    EXPECT_LE(RadialMenu::FanRange(layout, 0, 40).Width, 2.0f * kPi + 1e-4f);
    EXPECT_NEAR(RadialMenu::VariantSpan(layout, 0, 39, 40).End - RadialMenu::VariantSpan(layout, 0, 0, 40).Begin, 2.0f * kPi, 1e-3f);
}

TEST(RadialMenuMath, RingsAreByRadiusAndTheOuterOneIsUnbounded)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    const Vec2d c = layout.Center;
    EXPECT_EQ(RadialMenu::RingAt(layout, c), RadialMenu::Ring::Hub);
    EXPECT_EQ(RadialMenu::RingAt(layout, { c.X, c.Y - layout.Hub + 1.0f }), RadialMenu::Ring::Hub);
    EXPECT_EQ(RadialMenu::RingAt(layout, { c.X, c.Y - layout.Hub - 1.0f }), RadialMenu::Ring::Primary);
    EXPECT_EQ(RadialMenu::RingAt(layout, { c.X, c.Y - layout.RimOuter() + 1.0f }), RadialMenu::Ring::Primary);
    EXPECT_EQ(RadialMenu::RingAt(layout, { c.X, c.Y - layout.RimOuter() - 1.0f }), RadialMenu::Ring::Outer);
    EXPECT_EQ(RadialMenu::RingAt(layout, { c.X, c.Y - 5000.0f }), RadialMenu::Ring::Outer);
    // Without variants there is no outer band: the primary wedge reaches the edge.
    const RadialMenu::Layout plain = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 5, 0);
    EXPECT_EQ(RadialMenu::RingAt(plain, { plain.Center.X, plain.Center.Y - 5000.0f }), RadialMenu::Ring::Primary);
    EXPECT_FLOAT_EQ(plain.OuterRadius, 0.0f);
}

TEST(RadialMenuMath, VariantSlotsRoundTripAndLieInTheirParentsDirection)
{
    const RadialMenu::Layout layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 5, 4);
    for (int parent = 0; parent < 5; ++parent)
    {
        for (int i = 0; i < 4; ++i)
        {
            const Vec2d slot = RadialMenu::VariantSlotCenter(layout, parent, i, 4);
            EXPECT_EQ(RadialMenu::VariantAt(layout, parent, 4, slot), i) << "parent " << parent << " variant " << i;
            EXPECT_EQ(RadialMenu::RingAt(layout, slot), RadialMenu::Ring::Outer);
        }
        // The fan straddles the parent's own direction, and the neighbours'
        // directions are outside it.
        const Vec2d out = RadialMenu::SlotCenter(layout, parent);
        const Vec2d far{ layout.Center.X + (out.X - layout.Center.X) * 3.0f, layout.Center.Y + (out.Y - layout.Center.Y) * 3.0f };
        EXPECT_GE(RadialMenu::VariantAt(layout, parent, 4, far), 0);
        EXPECT_EQ(RadialMenu::VariantAt(layout, parent, 4, RadialMenu::SlotCenter(layout, (parent + 1) % 5)), -1);
        EXPECT_EQ(RadialMenu::VariantAt(layout, parent, 4, RadialMenu::SlotCenter(layout, (parent + 4) % 5)), -1);
    }
    EXPECT_EQ(RadialMenu::VariantAt(layout, 0, 0, layout.Center), -1);
    EXPECT_EQ(RadialMenu::VariantAt(layout, -1, 4, layout.Center), -1);
}

TEST(RadialMenuMath, PlaceReservesTheOuterRingOnlyWhenAToolHasVariants)
{
    const RadialMenu::Frame frame = Frame();
    const RadialMenu::Layout plain = RadialMenu::Place(frame, { 5.0f, 5.0f }, 5, 0);
    const RadialMenu::Layout ringed = RadialMenu::Place(frame, { 5.0f, 5.0f }, 5, 4);
    EXPECT_FLOAT_EQ(plain.Center.X, frame.Min.X + plain.RimOuter());
    EXPECT_FLOAT_EQ(ringed.Center.X, frame.Min.X + ringed.OuterExtent());
    EXPECT_GT(ringed.OuterExtent(), plain.OuterExtent());
    EXPECT_FLOAT_EQ(plain.OuterExtent(), plain.RimOuter());
    EXPECT_FLOAT_EQ(ringed.Center.Y, frame.Min.Y + ringed.OuterExtent());

    const RadialMenu::Layout corner = RadialMenu::Place(frame, { 1595.0f, 895.0f }, 5, 4);
    EXPECT_LE(corner.Center.X + corner.OuterExtent(), frame.Max.X);
    EXPECT_LE(corner.CaptionY() + corner.CaptionHeight, frame.Max.Y);
    EXPECT_GE(corner.CaptionY(), corner.Center.Y + corner.OuterExtent());

    // Scaled once, like every other field.
    const RadialMenu::Layout one = RadialMenu::Place(Frame(1.0f), { 800.0f, 450.0f }, 5, 4);
    const RadialMenu::Layout two = RadialMenu::Place(Frame(2.0f), { 800.0f, 450.0f }, 5, 4);
    EXPECT_FLOAT_EQ(two.Seam, one.Seam * 2.0f);
    EXPECT_FLOAT_EQ(two.Rim, one.Rim * 2.0f);
    EXPECT_FLOAT_EQ(two.OuterRadius, one.OuterRadius * 2.0f);
}

// Pointer intent: which entry a pointer means, from where it is, the entry it
// held, and where its way out to that entry's fan began. Eight entries, a
// fan of five on each: the fan reaches past the entry's wedge on both sides.
namespace
{
struct IntentWheel
{
    RadialMenu::Layout Layout = RadialMenu::Place(Frame(), { 800.0f, 450.0f }, 8, 5);
    std::vector<int> Fans = std::vector<int>(8, 5);

    [[nodiscard]] Vec2d Toward(Vec2d from, Vec2d to, float t) const
    {
        return { from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t };
    }
    [[nodiscard]] Vec2d Slot(int entry) const { return RadialMenu::SlotCenter(Layout, entry); }
    [[nodiscard]] Vec2d Variant(int entry, int index) const { return RadialMenu::VariantSlotCenter(Layout, entry, index, 5); }
};
}

TEST(RadialMenuIntent, EachHoldIsWhatThePointerStandsOnOrIsBoundFor)
{
    const IntentWheel w;
    const Vec2d slot = w.Slot(0);
    const Vec2d partWay = w.Toward(slot, w.Variant(0, 4), 0.55f);
    ASSERT_EQ(RadialMenu::SectorAt(w.Layout, partWay), 1);

    const RadialMenu::Pick hub = RadialMenu::PickAt(w.Layout, w.Fans, 0, slot, w.Layout.Center);
    EXPECT_EQ(hub.Entry, -1);
    EXPECT_EQ(hub.By, RadialMenu::Hold::None);

    const RadialMenu::Pick wedge = RadialMenu::PickAt(w.Layout, w.Fans, -1, w.Layout.Center, slot);
    EXPECT_EQ(wedge.Entry, 0);
    EXPECT_EQ(wedge.By, RadialMenu::Hold::Wedge);

    const RadialMenu::Pick transit = RadialMenu::PickAt(w.Layout, w.Fans, 0, slot, partWay);
    EXPECT_EQ(transit.Entry, 0);
    EXPECT_EQ(transit.Variant, -1);
    EXPECT_EQ(transit.By, RadialMenu::Hold::Transit);

    const RadialMenu::Pick variant = RadialMenu::PickAt(w.Layout, w.Fans, 0, partWay, w.Variant(0, 4));
    EXPECT_EQ(variant.Entry, 0);
    EXPECT_EQ(variant.Variant, 4);
    EXPECT_EQ(variant.By, RadialMenu::Hold::Variant);

    // The same point with nothing held, or a different entry held, is plain direction.
    EXPECT_EQ(RadialMenu::PickAt(w.Layout, w.Fans, -1, slot, partWay).Entry, 1);
    EXPECT_EQ(RadialMenu::PickAt(w.Layout, w.Fans, 2, w.Slot(2), partWay).By, RadialMenu::Hold::Wedge);
}

TEST(RadialMenuIntent, TheLineMustReachTheFanBeforeTheHubOrAnotherSlot)
{
    const IntentWheel w;
    const Vec2d slot = w.Slot(0);
    const Vec2d partWay = w.Toward(slot, w.Variant(0, 4), 0.55f);
    EXPECT_TRUE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, slot, partWay));

    // Toward the neighbour's slot: the slot comes first.
    const Vec2d towardSlot = w.Toward(slot, w.Slot(1), 0.7f);
    ASSERT_EQ(RadialMenu::SectorAt(w.Layout, towardSlot), 1);
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, slot, towardSlot));
    // Standing on the neighbour's slot, whichever way the pointer came.
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, w.Toward(slot, partWay, 0.5f), w.Slot(1)));
    // Back in from a variant, aimed at the hub.
    const Vec2d inward = w.Toward(w.Variant(0, 4), w.Layout.Center, 0.4f);
    ASSERT_EQ(RadialMenu::RingAt(w.Layout, inward), RadialMenu::Ring::Primary);
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, w.Variant(0, 4), inward));
    // Nothing to reach, no motion, or already past the rim.
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 0, slot, partWay));
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, partWay, partWay));
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, slot, w.Variant(0, 4)));
    // Away from this fan, out to the far side of the neighbour's.
    EXPECT_FALSE(RadialMenu::HeadsIntoFan(w.Layout, 0, 5, slot, w.Toward(slot, w.Variant(2, 2), 0.3f)));
}

TEST(RadialMenuIntent, TheCorridorFollowsTheLayoutNotFixedAngles)
{
    // Scaled, offset, crowded or sparse: the line to every flank variant from
    // the entry's slot is in transit wherever it crosses a neighbour's wedge.
    for (const float scale : { 1.0f, 2.0f })
    {
        for (const int count : { 3, 7, 11 })
        {
            const RadialMenu::Layout layout =
                RadialMenu::Place(Frame(scale, { 37.0f, 11.0f }, { 3237.0f, 1811.0f }), { 900.0f, 700.0f }, count, 5);
            for (int entry = 0; entry < count; ++entry)
            {
                for (const int flank : { 0, 4 })
                {
                    const Vec2d slot = RadialMenu::SlotCenter(layout, entry);
                    const Vec2d child = RadialMenu::VariantSlotCenter(layout, entry, flank, 5);
                    for (float t = 0.05f; t < 1.0f; t += 0.05f)
                    {
                        const Vec2d at{ slot.X + (child.X - slot.X) * t, slot.Y + (child.Y - slot.Y) * t };
                        if (RadialMenu::RingAt(layout, at) != RadialMenu::Ring::Primary || RadialMenu::SectorAt(layout, at) == entry)
                            continue;
                        EXPECT_TRUE(RadialMenu::HeadsIntoFan(layout, entry, 5, slot, at))
                            << "scale " << scale << ", " << count << " entries, entry " << entry << " flank " << flank << " t " << t;
                    }
                }
            }
        }
    }
}

TEST(RadialMenuIntent, TheWayBackIsTheEntrysOwnWedgeReachedFirst)
{
    const IntentWheel w;
    const Vec2d child = w.Variant(0, 4);
    const Vec2d back = w.Toward(child, w.Slot(0), 0.35f);
    ASSERT_EQ(RadialMenu::RingAt(w.Layout, back), RadialMenu::Ring::Primary);
    ASSERT_EQ(RadialMenu::SectorAt(w.Layout, back), 1);
    EXPECT_TRUE(RadialMenu::HeadsIntoWedge(w.Layout, 0, child, back));
    const RadialMenu::Pick held = RadialMenu::PickAt(w.Layout, w.Fans, 0, child, back);
    EXPECT_EQ(held.Entry, 0);
    EXPECT_EQ(held.Variant, -1);
    EXPECT_EQ(held.By, RadialMenu::Hold::Transit);

    // In toward the neighbour's slot, or the hub: not the way back.
    EXPECT_FALSE(RadialMenu::HeadsIntoWedge(w.Layout, 0, child, w.Toward(child, w.Slot(1), 0.35f)));
    EXPECT_FALSE(RadialMenu::HeadsIntoWedge(w.Layout, 0, child, w.Toward(child, w.Layout.Center, 0.35f)));
    // Leaving the wedge sideways does not count as returning to it, even a
    // hair past its edge where the crossing lies just behind the pointer.
    const RadialMenu::Span sector = RadialMenu::SectorSpan(0, 8);
    const auto onRing = [&](float angle) {
        return Vec2d{ w.Layout.Center.X + std::sin(angle) * w.Layout.Radius, w.Layout.Center.Y - std::cos(angle) * w.Layout.Radius };
    };
    for (const float past : { 1e-6f, 1e-3f, 0.05f })
    {
        const Vec2d before = onRing(sector.End - 0.05f);
        const Vec2d after = onRing(sector.End + past);
        ASSERT_EQ(RadialMenu::SectorAt(w.Layout, after), 1);
        EXPECT_FALSE(RadialMenu::HeadsIntoWedge(w.Layout, 0, before, after)) << past;
    }
    EXPECT_FALSE(RadialMenu::HeadsIntoWedge(w.Layout, 0, back, back));
    EXPECT_FALSE(RadialMenu::HeadsIntoWedge(w.Layout, -1, child, back));
}
