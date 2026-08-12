#include <gtest/gtest.h>

#include <net/ReplicationInterpolation.h>

#include <cmath>
#include <cstring>
#include <vector>

//=============================================================================
// What a client does with everything it does not simulate.
//
// The thing being protected is not accuracy, it is evenness. A mirrored entity
// drawn straight from each snapshot moves exactly as unevenly as its datagrams
// arrived: still on a tick whose snapshot was late, then twice as far when it
// lands. Holding poses with the tick they describe and presenting a tick in the
// past turns that back into constant motion, because the samples either side of
// a gap still describe the path through it.
//=============================================================================

namespace
{
    constexpr EntityId kPawn{ 7, 1 };
    constexpr EntityId kOther{ 9, 1 };

    LocalTransform PoseAt(float x)
    {
        LocalTransform pose;
        pose.Value.Position = Vec3d{ x, 0.0f, 0.0f };
        return pose;
    }

    LocalTransform PoseAt3(float x, float y, float z)
    {
        LocalTransform pose;
        pose.Value.Position = Vec3d{ x, y, z };
        return pose;
    }

    // A pawn walking a straight line at one metre a tick, so where it should be
    // at any tick is the tick number and any unevenness is visible as a step
    // that is not one.
    void Walk(ReplicationInterpolation& interpolation, std::uint64_t tick)
    {
        interpolation.Commit(kPawn, tick, PoseAt(static_cast<float>(tick)));
    }

    [[nodiscard]] float XAt(const ReplicationInterpolation& interpolation,
                            std::uint64_t tick)
    {
        const auto pose = interpolation.Resolve(kPawn, tick);
        return pose.has_value() ? pose->Value.Position.X : -1.0f;
    }
}

TEST(ReplicationInterpolation, SaysNothingAboutAnEntityItHasNotHeardOf)
{
    ReplicationInterpolation interpolation;
    EXPECT_FALSE(interpolation.Resolve(kPawn, 100).has_value());

    Walk(interpolation, 100);
    EXPECT_FALSE(interpolation.Resolve(kOther, 100).has_value())
        << "poses held for one entity say nothing about another";
}

TEST(ReplicationInterpolation, InterpolatesBetweenTheSamplesEitherSide)
{
    ReplicationInterpolation interpolation;
    interpolation.Commit(kPawn, 10, PoseAt(0.0f));
    interpolation.Commit(kPawn, 20, PoseAt(10.0f));

    EXPECT_FLOAT_EQ(XAt(interpolation, 10), 0.0f);
    EXPECT_FLOAT_EQ(XAt(interpolation, 15), 5.0f);
    EXPECT_FLOAT_EQ(XAt(interpolation, 20), 10.0f);
    EXPECT_GT(interpolation.Interpolated(), 0u);
}

// The reason a lost snapshot is allowed to cost nothing: the ticks it carried
// are still on the line between the ones that arrived.
TEST(ReplicationInterpolation, ATickThatNeverArrivedIsStillOnThePath)
{
    ReplicationInterpolation interpolation;
    Walk(interpolation, 10);
    // 11 and 12 lost.
    Walk(interpolation, 13);

    EXPECT_FLOAT_EQ(XAt(interpolation, 11), 11.0f)
        << "a tick that never arrived was not recovered from its neighbours";
    EXPECT_FLOAT_EQ(XAt(interpolation, 12), 12.0f);
}

namespace
{
    // A sender walking one tick per wall tick, whose datagrams arrive unevenly:
    // some wall ticks bring nothing, the ones after a stall bring several, and
    // tick 6 is lost outright and never resent.
    const std::vector<std::vector<std::uint64_t>>& UnevenDeliveries()
    {
        static const std::vector<std::vector<std::uint64_t>> deliveries = {
            { 1 }, {}, { 2, 3 }, {}, {}, { 4, 5, 7 }, { 8 }, {}, { 9, 10, 11 }, { 12 },
        };
        return deliveries;
    }

    // Runs that schedule and returns what a viewer would have been shown, one
    // entry per wall tick once the delay has been taken up.
    std::vector<float> PresentedWithDelay(std::uint64_t delayTicks)
    {
        ReplicationInterpolation interpolation;
        std::vector<float> presented;

        std::uint64_t wall = 1;
        for (const std::vector<std::uint64_t>& arriving : UnevenDeliveries())
        {
            for (const std::uint64_t tick : arriving)
                Walk(interpolation, tick);

            if (wall > delayTicks)
            {
                const auto pose = interpolation.Resolve(kPawn, wall - delayTicks);
                if (pose.has_value())
                    presented.push_back(pose->Value.Position.X);
            }
            ++wall;
        }
        return presented;
    }
}

// The whole feature, stated as the thing a viewer would notice: the sender's
// motion was even, so the presented motion is even, however unevenly the
// datagrams describing it turned up.
TEST(ReplicationInterpolation, PresentsEvenMotionFromUnevenArrivals)
{
    const std::vector<float> presented =
        PresentedWithDelay(ReplicationInterpolation::kDefaultDelayTicks + 1);

    ASSERT_GE(presented.size(), 6u);
    for (std::size_t index = 1; index < presented.size(); ++index)
    {
        EXPECT_NEAR(presented[index] - presented[index - 1], 1.0f, 1e-4f)
            << "step " << index << " covered "
            << (presented[index] - presented[index - 1])
            << " metres instead of one: the presented motion carries the "
               "unevenness of the arrivals, which is the stutter this exists "
               "to remove";
    }
}

// And the same schedule with no margin, so that the evenness above cannot be
// mistaken for something the buffer would have provided anyway. With nothing
// held back the presented tick runs past what has arrived on every stall, and
// the motion is the arrivals' shape exactly: stop, then catch up.
TEST(ReplicationInterpolation, WithoutTheDelayTheStutterIsBackImmediately)
{
    const std::vector<float> presented = PresentedWithDelay(0);

    ASSERT_GE(presented.size(), 6u);
    bool uneven = false;
    for (std::size_t index = 1; index < presented.size(); ++index)
    {
        if (std::abs((presented[index] - presented[index - 1]) - 1.0f) > 1e-4f)
            uneven = true;
    }
    EXPECT_TRUE(uneven)
        << "presenting with no margin produced even motion, so this schedule "
           "cannot demonstrate what the margin is for";
}

//-----------------------------------------------------------------------------
// The edges of the window
//-----------------------------------------------------------------------------

// Guessing past the newest sample means taking the guess back when the real one
// arrives, and taking it back is a snap in the opposite direction. Standing
// still is wrong in a way that resolves itself.
TEST(ReplicationInterpolationWindow, HoldsRatherThanExtrapolatesPastTheNewest)
{
    ReplicationInterpolation interpolation;
    Walk(interpolation, 10);
    Walk(interpolation, 11);

    EXPECT_FLOAT_EQ(XAt(interpolation, 20), 11.0f)
        << "the pose ran on past what the authority had actually said";
    EXPECT_GT(interpolation.Held(), 0u);
}

TEST(ReplicationInterpolationWindow, HoldsTheOldestBeforeTheWindowBegins)
{
    ReplicationInterpolation interpolation;
    Walk(interpolation, 50);
    Walk(interpolation, 51);

    EXPECT_FLOAT_EQ(XAt(interpolation, 10), 50.0f);
}

// The redundancy in the stream means the same tick arrives more than once, and
// the window is small enough that spending slots on repeats would push out the
// samples still needed to bracket the presented tick.
TEST(ReplicationInterpolationWindow, IgnoresATickItAlreadyHolds)
{
    ReplicationInterpolation interpolation;
    for (int repeat = 0; repeat < 20; ++repeat)
        Walk(interpolation, 30);
    Walk(interpolation, 40);

    EXPECT_FLOAT_EQ(XAt(interpolation, 35), 35.0f)
        << "repeats crowded out the sample needed to bracket this tick";
}

TEST(ReplicationInterpolationWindow, OrdersSamplesThatOvertakeEachOther)
{
    ReplicationInterpolation interpolation;
    Walk(interpolation, 10);
    Walk(interpolation, 14);  // overtakes
    Walk(interpolation, 12);

    EXPECT_FLOAT_EQ(XAt(interpolation, 11), 11.0f);
    EXPECT_FLOAT_EQ(XAt(interpolation, 13), 13.0f)
        << "a datagram that arrived out of order left the window unsorted, so "
           "the wrong pair bracketed this tick";
}

TEST(ReplicationInterpolationWindow, ForgetsAnEntityThatIsGone)
{
    ReplicationInterpolation interpolation;
    Walk(interpolation, 10);
    ASSERT_EQ(interpolation.TrackedCount(), 1u);

    interpolation.Forget(kPawn);
    EXPECT_EQ(interpolation.TrackedCount(), 0u);
    EXPECT_FALSE(interpolation.Resolve(kPawn, 10).has_value())
        << "a recycled handle would inherit the pose of whatever held it before";
}

//-----------------------------------------------------------------------------
// The authority's own view
//
// The same trap the predictor keeps a shadow for. A delta carries only what
// changed; applied to the pose being presented it would be applied to a blend
// this machine invented between two ticks, and every later delta would compound
// the difference.
//-----------------------------------------------------------------------------

TEST(ReplicationInterpolationShadow, KeepsTheAuthoritysValueApartFromTheBlend)
{
    ReplicationInterpolation interpolation;

    // Two poses far apart, so the pose presented between them is nothing like
    // either and the difference is impossible to miss.
    interpolation.Commit(kPawn, 10, PoseAt(0.0f));
    interpolation.Commit(kPawn, 20, PoseAt(100.0f));
    ASSERT_NE(interpolation.TryAuthoritative(kPawn), nullptr);

    ASSERT_FLOAT_EQ(XAt(interpolation, 15), 50.0f) << "sanity: presenting a blend";

    // The authority stands still, so the next snapshot carries no position at
    // all and the applier stages the delta onto what the shadow already holds.
    // Committing that must record where the authority actually was, not the
    // halfway pose just presented.
    interpolation.Commit(kPawn, 30, *interpolation.TryAuthoritative(kPawn));
    EXPECT_FLOAT_EQ(XAt(interpolation, 30), 100.0f)
        << "an unsent field was filled in from the blend this machine was "
           "drawing, so the entity drifted toward wherever it happened to be "
           "presented";
}

TEST(ReplicationInterpolationShadow, ForgettingAnEntityForgetsItsAuthority)
{
    ReplicationInterpolation interpolation;
    interpolation.Commit(kPawn, 10, PoseAt(5.0f));
    ASSERT_NE(interpolation.TryAuthoritative(kPawn), nullptr);

    interpolation.Forget(kPawn);
    EXPECT_EQ(interpolation.TryAuthoritative(kPawn), nullptr);
}

//-----------------------------------------------------------------------------
// Off
//-----------------------------------------------------------------------------

TEST(ReplicationInterpolation, DisabledInterceptsNothing)
{
    ReplicationInterpolation interpolation;
    ASSERT_TRUE(interpolation.InterceptsPose(ResolveComponentTypeId<LocalTransform>()));

    interpolation.SetEnabled(false);
    EXPECT_FALSE(interpolation.InterceptsPose(ResolveComponentTypeId<LocalTransform>()))
        << "off has to let arriving poses reach the world, or a mirrored entity "
           "never moves at all";
}

TEST(ReplicationInterpolation, WithholdsOnlyThePoseItPresents)
{
    ReplicationInterpolation interpolation;
    EXPECT_FALSE(interpolation.InterceptsPose(ResolveComponentTypeId<WorldTransform>()))
        << "everything else about a mirrored entity still lands normally";
}

// Omitting an entity that has nothing to say means a still entity's samples are
// far apart by design -- further apart than any loss would make them. Presenting
// one has to read as stillness rather than as a stream that has fallen behind.
TEST(ReplicationInterpolation, AStillEntityIsPresentedWhereItStands)
{
    ReplicationInterpolation interpolation;
    interpolation.SetSnapshotInterval(2);

    const EntityId entity{ 3, 1 };
    interpolation.Commit(entity, 1, PoseAt3(7.0f, 8.0f, 9.0f));
    interpolation.Commit(entity, 400, PoseAt3(7.0f, 8.0f, 9.0f));

    for (std::uint64_t tick : { std::uint64_t{ 2 }, std::uint64_t{ 200 },
                                std::uint64_t{ 400 } })
    {
        const std::optional<LocalTransform> pose = interpolation.Resolve(entity, tick);
        ASSERT_TRUE(pose.has_value()) << "tick " << tick;
        EXPECT_FLOAT_EQ(pose->Value.Position.X, 7.0f) << "tick " << tick;
        EXPECT_FLOAT_EQ(pose->Value.Position.Y, 8.0f) << "tick " << tick;
        EXPECT_FLOAT_EQ(pose->Value.Position.Z, 9.0f) << "tick " << tick;
    }
}
