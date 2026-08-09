#include <gtest/gtest.h>

#include <net/PawnCommandRing.h>

#include <vector>

//=============================================================================
// The ticks a client has simulated for its own pawn and not had answered.
//
// One ring rather than three samples of the same intent. What the simulation
// used, what the wire carries, and what a replay re-runs all come from here, so
// they cannot disagree -- and the acknowledgement that arrives back names
// records in it directly.
//=============================================================================

namespace
{
    PawnCommandTick At(std::uint64_t tick, float yaw = 0.0f)
    {
        PawnCommandTick record;
        record.Tick = tick;
        record.Yaw = yaw;
        record.Intent.WishDir = Vec3d(yaw, 0.0f, 0.0f);
        return record;
    }

    std::vector<std::uint64_t> Replayed(const PawnCommandRing& ring,
                                        std::uint64_t ack, bool& replayable)
    {
        std::vector<std::uint64_t> ticks;
        replayable = ring.ForEachAfter(
            ack, [&](const PawnCommandTick& record) { ticks.push_back(record.Tick); });
        return ticks;
    }
}

TEST(PawnCommandRing, HandsBackWhatTheAuthorityHasNotAnsweredForYet)
{
    PawnCommandRing ring;
    for (std::uint64_t tick = 10; tick <= 15; ++tick)
        ring.Push(At(tick));

    bool replayable = false;
    const std::vector<std::uint64_t> owed = Replayed(ring, 12, replayable);
    ASSERT_TRUE(replayable);
    EXPECT_EQ(owed, (std::vector<std::uint64_t>{ 13, 14, 15 }))
        << "replay has to start from the tick after the one confirmed, oldest "
           "first, or it re-runs input the authority already applied";
}

TEST(PawnCommandRing, AnAcknowledgementOfEverythingLeavesNothingToReplay)
{
    PawnCommandRing ring;
    for (std::uint64_t tick = 10; tick <= 15; ++tick)
        ring.Push(At(tick));

    bool replayable = false;
    EXPECT_TRUE(Replayed(ring, 15, replayable).empty());
    EXPECT_TRUE(replayable) << "nothing owed is not a gap";
}

TEST(PawnCommandRing, DropsWhatHasBeenConfirmed)
{
    PawnCommandRing ring;
    for (std::uint64_t tick = 1; tick <= 8; ++tick)
        ring.Push(At(tick));

    ring.DropThrough(5);
    EXPECT_EQ(ring.Size(), 3u);

    bool replayable = false;
    EXPECT_EQ(Replayed(ring, 5, replayable),
              (std::vector<std::uint64_t>{ 6, 7, 8 }));
    EXPECT_TRUE(replayable);
}

// The honest refusal. If the window has moved past a tick the authority has not
// confirmed, replaying what is left would skip input it is about to apply, and
// the result would be a pawn that quietly disagrees rather than one that snaps.
TEST(PawnCommandRing, RefusesWhenTheWindowHasMovedPastWhatIsOwed)
{
    PawnCommandRing ring;
    for (std::uint64_t tick = 1; tick <= PawnCommandRing::kDepth + 10; ++tick)
        ring.Push(At(tick));

    bool replayable = false;
    (void)Replayed(ring, 3, replayable);
    EXPECT_FALSE(replayable)
        << "a span with a hole in it was replayed as though it were complete";
}

TEST(PawnCommandRing, KeepsTheNewestWhenItOverflows)
{
    PawnCommandRing ring;
    const std::uint64_t last = PawnCommandRing::kDepth * 2;
    for (std::uint64_t tick = 1; tick <= last; ++tick)
        ring.Push(At(tick));

    EXPECT_EQ(ring.Size(), PawnCommandRing::kDepth);
    EXPECT_EQ(ring.NewestTick(), last);
    EXPECT_EQ(ring.Recent(0).Tick, last) << "newest first is what the wire wants";
    EXPECT_EQ(ring.Recent(1).Tick, last - 1);
}

TEST(PawnCommandRing, CarriesTheAimAndIntentEachTickRanWith)
{
    PawnCommandRing ring;
    ring.Push(At(40, 1.5f));
    ring.Push(At(41, 2.5f));

    EXPECT_FLOAT_EQ(ring.Recent(0).Yaw, 2.5f);
    EXPECT_FLOAT_EQ(ring.Recent(0).Intent.WishDir.X, 2.5f);
    EXPECT_FLOAT_EQ(ring.Recent(1).Yaw, 1.5f)
        << "a tick's aim belongs to that tick; sharing one across the window is "
           "what made the two machines frame the same input differently";
}

TEST(PawnCommandRing, ClearingLeavesNothingOwed)
{
    PawnCommandRing ring;
    ring.Push(At(5));
    ring.Clear();

    EXPECT_EQ(ring.Size(), 0u);
    bool replayable = false;
    EXPECT_TRUE(Replayed(ring, 0, replayable).empty());
}
