#include <gtest/gtest.h>

#include <net/NetTickEstimator.h>

#include <cstdint>

//=============================================================================
// Naming the authority's clock from a client.
//
// Two machines running the same fixed step count from their own process start,
// so a tick index means nothing across the wire until one of them says which
// name it uses. Everything a client will eventually do about latency -- ask
// for input on a tick, compare a prediction against what the authority
// believed at one -- rests on this number being right and, above all, steady.
//=============================================================================

namespace
{
    constexpr double kTick = 1.0 / 60.0;

    // Round trip in microseconds for a given one-way tick count.
    constexpr std::uint64_t RttForTicks(double ticks)
    {
        return static_cast<std::uint64_t>(ticks * kTick * 2.0 * 1e6);
    }
}

TEST(NetTickEstimator, ReportsNothingBeforeItHasHeardFromTheAuthority)
{
    NetTickEstimator clock;
    EXPECT_FALSE(clock.HasEstimate());
    EXPECT_EQ(clock.Offset(), 0);
}

// The sample is a round trip old, so the authority has moved on since it
// spoke. An estimate that took the number at face value would be permanently
// behind by exactly the latency it exists to account for.
TEST(NetTickEstimator, AgesTheSampleByItsFlightTime)
{
    NetTickEstimator clock;
    clock.Observe(1000, 400, RttForTicks(3), kTick);

    ASSERT_TRUE(clock.HasEstimate());
    EXPECT_EQ(clock.FlightTicks(), 3u);
    EXPECT_EQ(clock.AuthorityTickAt(400), 1003u);
    EXPECT_EQ(clock.Offset(), 603);
}

TEST(NetTickEstimator, TracksAnAuthorityThatStartedFirstOrLater)
{
    NetTickEstimator ahead;
    ahead.Observe(5000, 10, 0, kTick);
    EXPECT_EQ(ahead.AuthorityTickAt(10), 5000u);

    // The client started first; the offset is negative and must not wrap.
    NetTickEstimator behind;
    behind.Observe(10, 5000, 0, kTick);
    EXPECT_LT(behind.Offset(), 0);
    EXPECT_EQ(behind.AuthorityTickAt(5000), 10u);
    EXPECT_EQ(behind.AuthorityTickAt(3), 0u)
        << "there is nothing before tick zero to name";
}

// A command stamped for the present would arrive for a tick already simulated,
// every time, forever.
TEST(NetTickEstimator, StampsCommandsAheadOfWhereTheAuthorityIs)
{
    NetTickEstimator clock;
    clock.SetSlackTicks(1);
    clock.Observe(1000, 1000, RttForTicks(2), kTick);

    const std::uint64_t authority = clock.AuthorityTickAt(1000);
    const std::uint64_t command = clock.CommandTickAt(1000);
    EXPECT_GT(command, authority);
    // Flight out, plus the margin the authority asked for.
    EXPECT_EQ(command - authority, 2u + 1u);
    EXPECT_EQ(clock.CommandTickAt(1000),
              static_cast<std::uint64_t>(1000 + clock.CommandOffset()));
}

TEST(NetTickEstimator, MoreLatencyMeansStampingFurtherAhead)
{
    NetTickEstimator near;
    near.Observe(1000, 1000, RttForTicks(1), kTick);

    NetTickEstimator far;
    far.Observe(1000, 1000, RttForTicks(6), kTick);

    EXPECT_GT(far.CommandOffset(), near.CommandOffset());
}

// Rounding up matters: a flight time guessed short stamps commands that arrive
// after the tick they asked for, which is input lost. Guessed long, they wait
// one tick, which is input late.
TEST(NetTickEstimator, RoundsPartialFlightTimesUp)
{
    NetTickEstimator clock;
    clock.Observe(0, 0, RttForTicks(1.2), kTick);
    EXPECT_EQ(clock.FlightTicks(), 2u);

    NetTickEstimator zero;
    zero.Observe(0, 0, 0, kTick);
    EXPECT_EQ(zero.FlightTicks(), 0u);
}

//-----------------------------------------------------------------------------
// Steadiness
//
// The estimate is built from a measured round trip, so it is noisy. A stamp
// that jumped with the noise would reach the authority's arrival buffer as
// alternating gaps and backlog -- input lost and input late, from a connection
// that was merely normal.
//-----------------------------------------------------------------------------

TEST(NetTickEstimatorSteadiness, SlewsThroughJitterInsteadOfChasingIt)
{
    NetTickEstimator clock;
    clock.Observe(1000, 1000, RttForTicks(2), kTick);
    const std::int64_t settled = clock.Offset();

    // A sample four ticks off: real jitter, not a discontinuity.
    clock.Observe(1004, 1000, RttForTicks(2), kTick);
    const std::int64_t moved = clock.Offset() - settled;
    EXPECT_EQ(moved, NetTickEstimator::kSlewTicks)
        << "the estimate chased one noisy sample the whole way";
}

TEST(NetTickEstimatorSteadiness, ConvergesOnASustainedShift)
{
    NetTickEstimator clock;
    clock.Observe(1000, 1000, 0, kTick);

    // The authority is genuinely five ticks further along than first believed,
    // and keeps saying so.
    for (int sample = 0; sample < 20; ++sample)
        clock.Observe(1005, 1000, 0, kTick);

    EXPECT_EQ(clock.AuthorityTickAt(1000), 1005u)
        << "slewing has to arrive, not merely approach";
}

// Slewing a discontinuity one tick at a time would mis-stamp everything sent
// while it caught up -- seconds of it, for a second-wide correction.
TEST(NetTickEstimatorSteadiness, SnapsAcrossADiscontinuity)
{
    NetTickEstimator clock;
    clock.Observe(1000, 1000, 0, kTick);

    const std::uint64_t jumped = 1000 + NetTickEstimator::kSnapTicks * 10;
    clock.Observe(jumped, 1000, 0, kTick);
    EXPECT_EQ(clock.AuthorityTickAt(1000), jumped);
}

// A whole connection's worth of drift, driven in a loop: the client's clock
// runs slightly fast, and the estimate has to keep up without ever jumping.
TEST(NetTickEstimatorSteadiness, FollowsDriftWithoutJumping)
{
    NetTickEstimator clock;
    std::uint64_t local = 0;
    std::uint64_t authority = 0;
    clock.Observe(authority, local, RttForTicks(2), kTick);

    std::uint64_t previous = clock.AuthorityTickAt(local);
    for (int frame = 0; frame < 600; ++frame)
    {
        local += 1;
        // The authority falls behind one tick every sixty: a clock skew a real
        // pair of machines shows within minutes.
        if (frame % 60 != 0)
            authority += 1;

        clock.Observe(authority, local, RttForTicks(2), kTick);

        const std::uint64_t now = clock.AuthorityTickAt(local);
        ASSERT_GE(now, previous) << "the estimate went backwards at frame " << frame;
        ASSERT_LE(now - previous, 2u)
            << "the estimate jumped at frame " << frame;
        previous = now;
    }

    // Ten ticks of skew accumulated, and the estimate absorbed all of it.
    EXPECT_EQ(clock.AuthorityTickAt(local), authority + clock.FlightTicks());
}

TEST(NetTickEstimator, IgnoresANonsenseTickRate)
{
    NetTickEstimator clock;
    clock.Observe(1000, 0, 16000, 0.0);
    EXPECT_FALSE(clock.HasEstimate())
        << "dividing a flight time by a zero tick has no answer to record";
}

TEST(NetTickEstimator, ResetsWithTheSession)
{
    NetTickEstimator clock;
    clock.SetSlackTicks(4);
    clock.Observe(1000, 0, 16000, kTick);
    ASSERT_TRUE(clock.HasEstimate());

    clock.Reset();
    EXPECT_FALSE(clock.HasEstimate());
    EXPECT_EQ(clock.Offset(), 0);
    EXPECT_EQ(clock.SlackTicks(), 0u);
}
