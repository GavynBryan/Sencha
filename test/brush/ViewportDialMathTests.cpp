// Placement for the rotation dial that floats in a face plane. The panel draws
// it and the tool hit-tests it, so the rule they share is worth pinning alone.
#include "viewport/ViewportDialMath.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace
{
// A wall facing the viewer: the plane's normal is AxisU x AxisV.
const Vec3d kCenter{ 1.0f, 2.0f, 3.0f };
const Vec3d kAxisU{ 0.0f, 0.0f, -1.0f };
const Vec3d kAxisV{ 0.0f, 1.0f, 0.0f };
const Vec3d kFacingTheViewer{ 1.0f, 0.0f, 0.0f };

ViewportDial::Placement PlaceWith(float boxSemiMinor, float worldPerPixel, float scale = 1.0f)
{
    return ViewportDial::Place(kCenter, kAxisU, kAxisV, kFacingTheViewer, boxSemiMinor,
                               worldPerPixel, scale);
}
}

TEST(ViewportDial, TheRadiusIsGeometricUntilTheCapTakesOver)
{
    // Far enough away that half the box's smaller half-extent is under the cap:
    // the radius is purely geometric, so the rim stays well inside the shape.
    const ViewportDial::Placement far = PlaceWith(2.0f, 0.05f);
    ASSERT_TRUE(far.Visible);
    EXPECT_FLOAT_EQ(far.Radius, ViewportDial::kFraction * 2.0f);

    // Close up, the pixel cap takes over so the dial stops growing.
    const ViewportDial::Placement close = PlaceWith(2.0f, 0.001f);
    ASSERT_TRUE(close.Visible);
    EXPECT_LT(close.Radius, ViewportDial::kFraction * 2.0f);
    EXPECT_FLOAT_EQ(close.Radius, 0.001f * ViewportDial::kMaxPixels);
}

TEST(ViewportDial, TheRimCannotReachACornerHandle)
{
    // The nearest corner of a box with half-extents (p, q) is at least the
    // smaller of them away from the centre, and the rim is at half that.
    for (float semiMinor : { 0.1f, 1.0f, 7.5f })
    {
        const ViewportDial::Placement placement = PlaceWith(semiMinor, semiMinor * 0.002f);
        ASSERT_TRUE(placement.Visible) << semiMinor;
        EXPECT_LT(placement.Radius, semiMinor) << "the rim reached the corner handles";
    }
}

TEST(ViewportDial, ATinyBoxHidesTheDialRatherThanGrowingIt)
{
    // A widget that outgrew the thing it controls would be worse than one that
    // is briefly unavailable; the panel still has the numeric control.
    const ViewportDial::Placement tiny = PlaceWith(0.02f, 0.01f); // radius is one pixel
    EXPECT_FALSE(tiny.Visible);

    // The floor scales with the UI, so what hides at one scale hides at two.
    const ViewportDial::Placement scaled = PlaceWith(0.5f, 0.01f, 4.0f);
    EXPECT_FALSE(scaled.Visible) << "the visibility floor ignored the UI scale";
    EXPECT_TRUE(PlaceWith(0.5f, 0.01f, 1.0f).Visible);
}

TEST(ViewportDial, TheCapAndTheFloorBothFollowTheUiScale)
{
    const ViewportDial::Placement normal = PlaceWith(100.0f, 0.01f, 1.0f);
    const ViewportDial::Placement doubled = PlaceWith(100.0f, 0.01f, 2.0f);
    ASSERT_TRUE(normal.Visible);
    ASSERT_TRUE(doubled.Visible);
    EXPECT_FLOAT_EQ(doubled.Radius, normal.Radius * 2.0f);
}

TEST(ViewportDial, AFaceSeenEdgeOnHidesTheDial)
{
    const Vec3d alongThePlane{ 0.0f, 1.0f, 0.0f };
    const ViewportDial::Placement placement = ViewportDial::Place(
        kCenter, kAxisU, kAxisV, alongThePlane, 2.0f, 0.01f, 1.0f);
    EXPECT_FALSE(placement.Visible) << "an angle nobody can read should not be draggable";
}

TEST(ViewportDial, ADegenerateBoxHasNoDial)
{
    EXPECT_FALSE(PlaceWith(0.0f, 0.01f).Visible);
    EXPECT_FALSE(PlaceWith(2.0f, 0.0f).Visible);
}

TEST(ViewportDial, TheRimLiesInThePlaneOnTheCircle)
{
    const ViewportDial::Placement placement = PlaceWith(2.0f, 0.01f);
    ASSERT_TRUE(placement.Visible);
    std::array<Vec3d, ViewportDial::kRimSegments + 1> rim{};
    const int count = ViewportDial::RimPoints(placement, rim);
    ASSERT_EQ(count, ViewportDial::kRimSegments + 1);

    const Vec3d normal = kAxisU.Cross(kAxisV);
    for (int i = 0; i < count; ++i)
    {
        const Vec3d offset = rim[static_cast<std::size_t>(i)] - placement.Center;
        EXPECT_NEAR(offset.Dot(normal), 0.0f, 1e-5f) << "rim point " << i << " left the plane";
        EXPECT_NEAR(offset.Magnitude(), placement.Radius, 1e-5f) << "rim point " << i;
    }
    // The ring closes on itself.
    EXPECT_NEAR((rim[0] - rim[static_cast<std::size_t>(count - 1)]).Magnitude(), 0.0f, 1e-5f);
}

TEST(ViewportDial, ThePointAtAnAngleFollowsTheFrameSBasis)
{
    const ViewportDial::Placement placement = PlaceWith(2.0f, 0.01f);
    EXPECT_NEAR(((placement.PointAt(0.0f) - placement.Center) - kAxisU * placement.Radius).Magnitude(),
                0.0f, 1e-5f);
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    EXPECT_NEAR(((placement.PointAt(quarter) - placement.Center) - kAxisV * placement.Radius)
                    .Magnitude(),
                0.0f, 1e-4f)
        << "a positive quarter turn should reach +V";
}

TEST(ViewportDial, TicksLandOnTheStopsAndVanishWithoutAnIncrement)
{
    const ViewportDial::Placement placement = PlaceWith(2.0f, 0.01f);
    std::array<Vec3d, 32> ticks{};
    const float eighth = std::numbers::pi_v<float> / 4.0f;
    const int count = ViewportDial::TickPoints(placement, eighth, ticks);
    EXPECT_EQ(count, 8);
    for (int i = 0; i < count; ++i)
        EXPECT_NEAR((ticks[static_cast<std::size_t>(i)]
                     - placement.PointAt(static_cast<float>(i) * eighth))
                        .Magnitude(),
                    0.0f, 1e-5f);

    EXPECT_EQ(ViewportDial::TickPoints(placement, 0.0f, ticks), 0) << "free rotation has no stops";
}

TEST(ViewportDial, TheRimDistanceIsSymmetricAboutIt)
{
    // A projected ring of radius 100 about (200, 200).
    std::vector<std::optional<ImVec2>> rim;
    for (int i = 0; i <= ViewportDial::kRimSegments; ++i)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i)
                            / static_cast<float>(ViewportDial::kRimSegments);
        rim.push_back(ImVec2(200.0f + 100.0f * std::cos(angle), 200.0f + 100.0f * std::sin(angle)));
    }

    const std::optional<float> outside = ViewportDial::DistanceToRim(rim, ImVec2(310.0f, 200.0f));
    const std::optional<float> inside = ViewportDial::DistanceToRim(rim, ImVec2(290.0f, 200.0f));
    ASSERT_TRUE(outside.has_value());
    ASSERT_TRUE(inside.has_value());
    EXPECT_NEAR(*outside, 10.0f, 0.5f);
    EXPECT_NEAR(*inside, 10.0f, 0.5f) << "the rim's reach should be the same from either side";

    // The centre is a radius away, so it is not on the rim.
    EXPECT_GT(*ViewportDial::DistanceToRim(rim, ImVec2(200.0f, 200.0f)), 90.0f);

    // A rim entirely behind the camera cannot be measured against.
    const std::vector<std::optional<ImVec2>> gone(rim.size(), std::nullopt);
    EXPECT_FALSE(ViewportDial::DistanceToRim(gone, ImVec2(0.0f, 0.0f)).has_value());
}
