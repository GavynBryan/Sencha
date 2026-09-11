#include "brush/CarveShape.h"
#include "brush/CarveSurround.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <numbers>

namespace
{
constexpr float kWeldTol = 1e-4f;

struct Box
{
    Vec2d Min, Max;
    const char* Name;
};

// The three aspect ratios that separate an ellipse from a circular segment: a
// circular cap over the tall narrow one leaves the box before rise 0.2.
const Box kBoxes[] = {
    { Vec2d{ 0, 0 }, Vec2d{ 4, 1 }, "wide" },
    { Vec2d{ 0, 0 }, Vec2d{ 2, 2 }, "square" },
    { Vec2d{ 0, 0 }, Vec2d{ 1, 3 }, "tall and narrow" },
};

bool IsCounterClockwise(const std::vector<Vec2d>& outline)
{
    return PolygonSignedArea(outline) > 0.0f;
}

float ShortestEdge(const std::vector<Vec2d>& outline)
{
    float shortest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < outline.size(); ++i)
    {
        const Vec2d& a = outline[i];
        const Vec2d& b = outline[(i + 1) % outline.size()];
        shortest = std::min(shortest, std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y)));
    }
    return shortest;
}
}

TEST(CarveShape, RiseZeroIsTheRectangle)
{
    const CarveShapeParams flat{ 0.0f, 8 };
    for (const Box& box : kBoxes)
    {
        const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, flat);
        const std::vector<Vec2d> rect =
            CarveShapeOutline(CarveShape::Rectangle, box.Min, box.Max, flat);
        ASSERT_EQ(arch.size(), rect.size()) << box.Name;
        for (std::size_t i = 0; i < arch.size(); ++i)
        {
            EXPECT_NEAR(arch[i].X, rect[i].X, 1e-6f) << box.Name;
            EXPECT_NEAR(arch[i].Y, rect[i].Y, 1e-6f) << box.Name;
        }
    }
}

TEST(CarveShape, RiseOneSpringsFromTheFloorWithNoJambs)
{
    const CarveShapeParams full{ 1.0f, 8 };
    for (const Box& box : kBoxes)
    {
        const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, full);
        // The arc alone: its own ends are the bottom corners, so there are no
        // jamb vertices sitting on top of them.
        EXPECT_EQ(arch.size(), static_cast<std::size_t>(full.ArchSegments) + 1) << box.Name;
        EXPECT_NEAR(arch.front().Y, box.Min.Y, 1e-5f) << box.Name;
        EXPECT_NEAR(arch.back().Y, box.Min.Y, 1e-5f) << box.Name;
        EXPECT_GT(ShortestEdge(arch), 1e-3f) << box.Name << ": a corner collapsed into a sliver";
    }
}

TEST(CarveShape, TheOutlineStaysInsideTheBoxAtEveryRise)
{
    // The reason the arch is elliptical. A circular segment honouring the same
    // rise would bulge past the jambs on the tall narrow box.
    for (const Box& box : kBoxes)
        for (int step = 0; step <= 20; ++step)
        {
            const CarveShapeParams params{ static_cast<float>(step) / 20.0f, 12 };
            const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, params);
            for (const Vec2d& p : arch)
            {
                EXPECT_GE(p.X, box.Min.X - 1e-5f) << box.Name << " at rise " << params.ArchRise;
                EXPECT_LE(p.X, box.Max.X + 1e-5f) << box.Name << " at rise " << params.ArchRise;
                EXPECT_GE(p.Y, box.Min.Y - 1e-5f) << box.Name << " at rise " << params.ArchRise;
                EXPECT_LE(p.Y, box.Max.Y + 1e-5f) << box.Name << " at rise " << params.ArchRise;
            }
            EXPECT_TRUE(IsCounterClockwise(arch)) << box.Name << " at rise " << params.ArchRise;
        }
}

TEST(CarveShape, TheApexReachesTheTopOfTheBox)
{
    for (const Box& box : kBoxes)
        for (int step = 1; step <= 20; ++step)
        {
            const CarveShapeParams params{ static_cast<float>(step) / 20.0f, 12 };
            const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, params);
            float top = box.Min.Y;
            for (const Vec2d& p : arch)
                top = std::max(top, p.Y);
            EXPECT_NEAR(top, box.Max.Y, 1e-5f) << box.Name << " at rise " << params.ArchRise;
        }
}

TEST(CarveShape, TheGeneratorHonoursItsSegmentCountExactly)
{
    // The generator never clamps; the range below is what refuses a count.
    for (int segments : { 2, 3, 7, 16, 64 })
    {
        const CarveShapeParams params{ 0.5f, segments };
        const std::vector<Vec2d> arch =
            CarveShapeOutline(CarveShape::Arch, Vec2d{ 0, 0 }, Vec2d{ 2, 2 }, params);
        EXPECT_EQ(arch.size(), static_cast<std::size_t>(segments) + 3) << "segments " << segments;
    }
}

TEST(CarveShape, ARiseOfHalfTheAspectRatioDrawsATrueCircle)
{
    // The Roman arch stays reachable: the two semi-axes are equal exactly when
    // rise is width over twice the height.
    const Vec2d mn{ 0, 0 };
    const Vec2d mx{ 2, 3 };
    const float rise = (mx.X - mn.X) / (2.0f * (mx.Y - mn.Y));
    const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, mn, mx, { rise, 16 });

    const float radius = (mx.X - mn.X) * 0.5f;
    const Vec2d center{ (mn.X + mx.X) * 0.5f, mx.Y - radius };
    for (const Vec2d& p : arch)
    {
        if (p.Y < center.Y - 1e-4f)
            continue; // a jamb, below the springline
        const float distance =
            std::sqrt((p.X - center.X) * (p.X - center.X) + (p.Y - center.Y) * (p.Y - center.Y));
        EXPECT_NEAR(distance, radius, 1e-4f);
    }
}

TEST(CarveShape, TheRangeIsWhatRefusesASubWeldChord)
{
    const Vec2d mn{ 0, 0 };
    const Vec2d mx{ 2, 2 };
    const CarveShapeLimits limits = CarveShapeRange(mn, mx, 0.5f, 0.0f, kWeldTol);
    ASSERT_FALSE(limits.Empty());
    EXPECT_EQ(limits.MinSegments, 2);

    const std::vector<Vec2d> legal = CarveShapeOutline(CarveShape::Arch, mn, mx, { 0.5f, limits.MaxSegments });
    EXPECT_GE(ShortestEdge(legal), kWeldTol);

    // Past the cap the generator still obeys, and the chord goes under: the
    // refusal has to come from the range, which is why the tool clamps there.
    const std::vector<Vec2d> tooFine =
        CarveShapeOutline(CarveShape::Arch, mn, mx, { 0.5f, limits.MaxSegments * 4 });
    EXPECT_LT(ShortestEdge(tooFine), kWeldTol);
}

TEST(CarveShape, ARiseTooSmallForAnyArcReportsAnEmptyRange)
{
    const CarveShapeLimits limits = CarveShapeRange(Vec2d{ 0, 0 }, Vec2d{ 2, 2 }, 1e-6f, 0.0f, kWeldTol);
    EXPECT_TRUE(limits.Empty()) << "an arc no segment count can draw should say so";
}

TEST(CarveShape, OutlinesAreSimple)
{
    for (const Box& box : kBoxes)
        for (int step = 0; step <= 10; ++step)
        {
            const CarveShapeParams params{ static_cast<float>(step) / 10.0f, 9 };
            const std::vector<Vec2d> arch = CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, params);
            for (std::size_t i = 0; i < arch.size(); ++i)
                for (std::size_t j = i + 1; j < arch.size(); ++j)
                    EXPECT_GT(std::abs(arch[i].X - arch[j].X) + std::abs(arch[i].Y - arch[j].Y), 1e-6f)
                        << box.Name << " at rise " << params.ArchRise << ": repeated vertex";
        }
}

namespace
{
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;
constexpr float kEighthTurn = std::numbers::pi_v<float> / 4.0f;

// The two constraints the turned rectangle has to satisfy: its own upright
// extent must fit inside the box.
bool FitsInBox(Vec2d semi, float p, float q, float angle)
{
    const float c = std::abs(std::cos(angle));
    const float s = std::abs(std::sin(angle));
    const float slack = 1e-4f * std::max(p, q);
    return semi.X * c + semi.Y * s <= p + slack && semi.X * s + semi.Y * c <= q + slack;
}
}

TEST(CarveShape, TheInscribedRectangleIsTheBoxOnTheAxes)
{
    // A quarter turn hands back the box with its extents swapped, which turned
    // back covers the box exactly. That is what lets a quarter turn leave an
    // opening alone and only turn what is cut out of it. To tolerance, not
    // exactly: the cosine of a float quarter turn is about -4e-8, not zero.
    for (const auto& [p, q] : { std::pair{ 2.0f, 1.0f }, std::pair{ 1.0f, 3.0f },
                                std::pair{ 1.5f, 1.5f } })
    {
        const Vec2d flat = LargestInscribedRotatedRectangle(p, q, 0.0f);
        EXPECT_NEAR(flat.X, p, 1e-5f);
        EXPECT_NEAR(flat.Y, q, 1e-5f);

        const Vec2d turned = LargestInscribedRotatedRectangle(p, q, kQuarterTurn);
        EXPECT_NEAR(turned.X, q, 1e-5f);
        EXPECT_NEAR(turned.Y, p, 1e-5f);

        // Half and three-quarter turns repeat the first two.
        const Vec2d half = LargestInscribedRotatedRectangle(p, q, 2.0f * kQuarterTurn);
        EXPECT_NEAR(half.X, p, 1e-5f);
        EXPECT_NEAR(half.Y, q, 1e-5f);
        const Vec2d threeQuarters = LargestInscribedRotatedRectangle(p, q, 3.0f * kQuarterTurn);
        EXPECT_NEAR(threeQuarters.X, q, 1e-5f);
        EXPECT_NEAR(threeQuarters.Y, p, 1e-5f);
    }
}

TEST(CarveShape, TheInscribedRectangleSurvivesTheDiagonal)
{
    // A square box at exactly a float eighth turn reaches the simultaneous solve:
    // the cosine and sine land about 3e-8 apart while twice their product rounds
    // to just under one, so both single-constraint tests fail. That is the
    // ill-conditioned corner the diagonal case exists for, and the answer there
    // has to be the diamond rather than anything the divisor's size produced.
    const Vec2d semi = LargestInscribedRotatedRectangle(1.0f, 1.0f, kEighthTurn);
    ASSERT_TRUE(std::isfinite(semi.X)) << "the diagonal produced a non-finite half-extent";
    ASSERT_TRUE(std::isfinite(semi.Y));
    EXPECT_GT(semi.X, 0.0f);
    EXPECT_GT(semi.Y, 0.0f);
    EXPECT_TRUE(FitsInBox(semi, 1.0f, 1.0f, kEighthTurn));

    // It is the inscribed diamond: a square of half-diagonal 1, so half the area.
    EXPECT_NEAR(semi.X, semi.Y, 1e-5f);
    EXPECT_NEAR(semi.X, 1.0f / std::numbers::sqrt2_v<float>, 1e-4f);
    EXPECT_NEAR(4.0f * semi.X * semi.Y, 0.5f * 4.0f, 1e-3f);
}

TEST(CarveShape, TheInscribedRectangleStaysContinuousAcrossTheDiagonal)
{
    // A near-square box swept finely through the eighth turn crosses the branch
    // boundary. Nothing may jump, blow up or go non-finite there.
    const float p = 1.0f;
    const float q = 1.0f + 1e-4f;
    Vec2d previous = LargestInscribedRotatedRectangle(p, q, kEighthTurn - 0.01f);
    for (int i = -1000; i <= 1000; ++i)
    {
        const float angle = kEighthTurn + static_cast<float>(i) * 1e-5f;
        const Vec2d semi = LargestInscribedRotatedRectangle(p, q, angle);
        ASSERT_TRUE(std::isfinite(semi.X) && std::isfinite(semi.Y)) << "at step " << i;
        ASSERT_GT(semi.X, 0.0f) << "at step " << i;
        ASSERT_GT(semi.Y, 0.0f) << "at step " << i;
        ASSERT_TRUE(FitsInBox(semi, p, q, angle)) << "at step " << i;
        EXPECT_LT((semi - previous).Magnitude(), 1e-2f) << "the solve jumped at step " << i;
        previous = semi;
    }
}

TEST(CarveShape, TheInscribedRectangleIsTheLargestOneThatFits)
{
    // Nothing sampled inside the box beats what the solve returns.
    std::mt19937 rng(20260911u);
    std::uniform_real_distribution<float> size(0.2f, 4.0f);
    std::uniform_real_distribution<float> turn(-6.3f, 6.3f);
    for (int trial = 0; trial < 300; ++trial)
    {
        const float p = size(rng);
        const float q = size(rng);
        const float angle = turn(rng);
        const Vec2d semi = LargestInscribedRotatedRectangle(p, q, angle);
        ASSERT_TRUE(FitsInBox(semi, p, q, angle)) << "p=" << p << " q=" << q << " angle=" << angle;
        ASSERT_GT(semi.X, 0.0f);
        ASSERT_GT(semi.Y, 0.0f);
        const float best = semi.X * semi.Y;

        for (int i = 1; i <= 60; ++i)
            for (int j = 1; j <= 60; ++j)
            {
                const Vec2d probe{ p * static_cast<float>(i) / 30.0f,
                                   q * static_cast<float>(j) / 30.0f };
                if (FitsInBox(probe, p, q, angle))
                {
                    EXPECT_LE(probe.X * probe.Y, best * 1.01f)
                        << "a sampled rectangle beat the solve at angle " << angle;
                }
            }
    }
}

TEST(CarveShape, ADegenerateBoxHasNoInscribedRectangle)
{
    EXPECT_EQ(LargestInscribedRotatedRectangle(0.0f, 1.0f, 0.3f).X, 0.0f);
    EXPECT_EQ(LargestInscribedRotatedRectangle(1.0f, -1.0f, 0.3f).Y, 0.0f);
}

TEST(CarveShape, AQuarterTurnStillFillsTheBox)
{
    // The whole point of turning the shape rather than the box: the opening the
    // user drew is unchanged and only what is cut out of it turns.
    for (const Box& box : kBoxes)
        for (float turn : { kQuarterTurn, -kQuarterTurn, 2.0f * kQuarterTurn })
            for (CarveShape shape : { CarveShape::Rectangle, CarveShape::Arch })
            {
                const std::vector<Vec2d> outline =
                    CarveShapeOutline(shape, box.Min, box.Max, { 0.5f, 8, turn });
                Vec2d mn = outline.front();
                Vec2d mx = outline.front();
                for (const Vec2d& p : outline)
                {
                    mn.X = std::min(mn.X, p.X);
                    mn.Y = std::min(mn.Y, p.Y);
                    mx.X = std::max(mx.X, p.X);
                    mx.Y = std::max(mx.Y, p.Y);
                }
                EXPECT_NEAR(mn.X, box.Min.X, 1e-4f) << box.Name << " at " << turn;
                EXPECT_NEAR(mn.Y, box.Min.Y, 1e-4f) << box.Name << " at " << turn;
                EXPECT_NEAR(mx.X, box.Max.X, 1e-4f) << box.Name << " at " << turn;
                EXPECT_NEAR(mx.Y, box.Max.Y, 1e-4f) << box.Name << " at " << turn;
            }
}

TEST(CarveShape, APositiveTurnCarriesTheApexFromPlusVToMinusU)
{
    // The sign convention, asserted as a direction rather than as a bound.
    // Positive is a right-handed turn about the face normal, and since
    // AxisU x AxisV == Normal that is the ordinary counter-clockwise turn here.
    const Vec2d mn{ 0, 0 };
    const Vec2d mx{ 2, 2 };
    const Vec2d centre{ 1, 1 };

    // The apex is found structurally rather than by distance: the generator emits
    // two jamb feet and then the arc from one springline round to the other, so
    // the crown is the arc's midpoint. Picking the point furthest from the centre
    // would find a jamb foot instead, which is further out than the crown.
    constexpr int kSegments = 12;
    const auto apexOf = [&](float turn) {
        const std::vector<Vec2d> outline =
            CarveShapeOutline(CarveShape::Arch, mn, mx, { 0.6f, kSegments, turn });
        EXPECT_EQ(outline.size(), static_cast<std::size_t>(kSegments) + 3);
        return outline[2 + kSegments / 2];
    };

    EXPECT_GT(apexOf(0.0f).Y, centre.Y) << "at rest the apex should be on +V";
    EXPECT_LT(apexOf(kQuarterTurn).X, centre.X) << "a positive quarter turn should reach -U";
    EXPECT_GT(apexOf(-kQuarterTurn).X, centre.X) << "a negative quarter turn should reach +U";
    EXPECT_LT(apexOf(2.0f * kQuarterTurn).Y, centre.Y) << "a half turn should reach -V";
}

TEST(CarveShape, EveryTurnKeepsTheOutlineInsideTheBox)
{
    for (const Box& box : kBoxes)
        for (int step = 0; step < 32; ++step)
        {
            const float turn = static_cast<float>(step) * std::numbers::pi_v<float> / 8.0f;
            for (int riseStep = 0; riseStep <= 4; ++riseStep)
            {
                const CarveShapeParams params{ static_cast<float>(riseStep) / 4.0f, 9, turn };
                const std::vector<Vec2d> arch =
                    CarveShapeOutline(CarveShape::Arch, box.Min, box.Max, params);
                for (const Vec2d& p : arch)
                {
                    EXPECT_GE(p.X, box.Min.X - 1e-4f) << box.Name << " at " << turn;
                    EXPECT_LE(p.X, box.Max.X + 1e-4f) << box.Name << " at " << turn;
                    EXPECT_GE(p.Y, box.Min.Y - 1e-4f) << box.Name << " at " << turn;
                    EXPECT_LE(p.Y, box.Max.Y + 1e-4f) << box.Name << " at " << turn;
                }
                EXPECT_TRUE(IsCounterClockwise(arch)) << box.Name << " at " << turn;
            }
        }
}

TEST(CarveShape, TheGeneratorHonoursTheTurnExactlyAndNeverClamps)
{
    // A turn of a degree is a turn of a degree, not a snap to the nearest axis.
    const Vec2d mn{ 0, 0 };
    const Vec2d mx{ 3, 2 };
    const std::vector<Vec2d> rest = CarveShapeOutline(CarveShape::Arch, mn, mx, { 0.5f, 8, 0.0f });
    const float degree = std::numbers::pi_v<float> / 180.0f;
    const std::vector<Vec2d> nudged = CarveShapeOutline(CarveShape::Arch, mn, mx, { 0.5f, 8, degree });
    ASSERT_EQ(rest.size(), nudged.size());
    float moved = 0.0f;
    for (std::size_t i = 0; i < rest.size(); ++i)
        moved = std::max(moved, (rest[i] - nudged[i]).Magnitude());
    EXPECT_GT(moved, 1e-3f) << "a one-degree turn was swallowed";
    EXPECT_LT(moved, 0.2f) << "a one-degree turn moved the outline like a large one";
}

TEST(CarveShape, TheSegmentRangeTightensWithTheTurn)
{
    // A turned arch has less room, so its chords are shorter and fewer segments
    // survive the weld tolerance.
    const Vec2d mn{ 0, 0 };
    const Vec2d mx{ 2, 2 };
    const CarveShapeLimits flat = CarveShapeRange(mn, mx, 0.5f, 0.0f, kWeldTol);
    const CarveShapeLimits turned = CarveShapeRange(mn, mx, 0.5f, kEighthTurn, kWeldTol);
    ASSERT_FALSE(flat.Empty());
    ASSERT_FALSE(turned.Empty());
    EXPECT_LT(turned.MaxSegments, flat.MaxSegments);
}

TEST(CarveShape, TheRiseRangeIsReportedRatherThanAssumed)
{
    const CarveShapeLimits limits = CarveShapeRange(Vec2d{ 0, 0 }, Vec2d{ 2, 2 }, 0.5f, 0.0f, kWeldTol);
    EXPECT_FLOAT_EQ(limits.MinRise, 0.0f);
    EXPECT_FLOAT_EQ(limits.MaxRise, 1.0f);
}

TEST(CarveShape, TheShapeFrameRoundTripsThroughItsOwnCoordinates)
{
    for (float turn : { 0.0f, kEighthTurn, kQuarterTurn, -1.1f })
    {
        const CarveShapeFrame frame = CarveShapeFrameFor(Vec2d{ -1, 2 }, Vec2d{ 5, 6 }, turn);
        for (const Vec2d& local : { Vec2d{ 0, 0 }, Vec2d{ 0.7f, -0.3f }, Vec2d{ -2.0f, 1.5f } })
        {
            const Vec2d back = frame.ToShape(frame.ToBox(local));
            EXPECT_NEAR(back.X, local.X, 1e-4f) << "at " << turn;
            EXPECT_NEAR(back.Y, local.Y, 1e-4f) << "at " << turn;
        }
    }
}

TEST(CarveShape, TheSpringlineRunsFromTheCrownToTheFloor)
{
    const CarveShapeFrame frame = CarveShapeFrameFor(Vec2d{ 0, 0 }, Vec2d{ 4, 2 }, 0.0f);
    EXPECT_FLOAT_EQ(frame.SpringlineAt(0.0f), frame.SemiV);   // no arch: flush with the top
    EXPECT_FLOAT_EQ(frame.SpringlineAt(1.0f), -frame.SemiV);  // springs from the floor
    EXPECT_FLOAT_EQ(frame.SpringlineAt(0.5f), 0.0f);
}

TEST(CarveShape, ARiseReadBackFromItsHandleIsTheRiseThatPlacedIt)
{
    // What the viewport drag does: put the handle on the springline, read the
    // cursor back into the shape's own frame, and keep only the rise axis.
    for (float turn : { 0.0f, kQuarterTurn, kEighthTurn, 2.3f })
    {
        const CarveShapeFrame frame = CarveShapeFrameFor(Vec2d{ -3, 1 }, Vec2d{ 1, 4 }, turn);
        ASSERT_GT(frame.SemiV, 0.0f);
        for (float rise : { 0.0f, 0.25f, 0.6f, 1.0f })
        {
            const Vec2d handle = frame.ToBox(Vec2d{ 0.0f, frame.SpringlineAt(rise) });
            const float recovered = (frame.SemiV - frame.ToShape(handle).Y) / (2.0f * frame.SemiV);
            EXPECT_NEAR(recovered, rise, 1e-4f) << "at turn " << turn;
        }
    }
}

TEST(CarveShape, ACursorAcrossTheRiseAxisDoesNotMoveTheRise)
{
    // The handle is a slider along the shape's rise, not a free point: whatever
    // the cursor does across that axis is discarded.
    for (float turn : { 0.0f, kQuarterTurn, kEighthTurn, -0.7f })
    {
        const CarveShapeFrame frame = CarveShapeFrameFor(Vec2d{ 0, 0 }, Vec2d{ 4, 3 }, turn);
        const float springline = frame.SpringlineAt(0.4f);
        const float centred = frame.ToShape(frame.ToBox(Vec2d{ 0.0f, springline })).Y;
        for (float across : { -1.3f, -0.2f, 0.5f, 1.7f })
        {
            const Vec2d offset = frame.ToBox(Vec2d{ across, springline });
            EXPECT_NEAR(frame.ToShape(offset).Y, centred, 1e-4f) << "at turn " << turn;
        }
    }
}
