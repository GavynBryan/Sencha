#include <gtest/gtest.h>

#include <graphics/PresentHistory.h>

#include <chrono>

namespace
{
    using Clock = PresentHistory::Clock;
    using std::chrono::milliseconds;

    const Clock::time_point kT0 = Clock::time_point{} + std::chrono::hours(1);
}

TEST(PresentHistory, IdBehindNextCountsBackFromThePresentAboutToBeQueued)
{
    PresentHistory history;
    EXPECT_EQ(history.IdBehindNext(1), 0u);

    history.Queued(10, kT0);
    history.Queued(11, kT0);
    history.Queued(12, kT0);

    EXPECT_EQ(history.IdBehindNext(0), 0u);
    EXPECT_EQ(history.IdBehindNext(1), 12u);
    EXPECT_EQ(history.IdBehindNext(3), 10u);
    // Nothing this chain queued is four presents back.
    EXPECT_EQ(history.IdBehindNext(4), 0u);
}

TEST(PresentHistory, ARebuiltChainHasNothingToWaitOn)
{
    PresentHistory history;
    history.Queued(5, kT0);
    history.Queued(6, kT0);
    history.Clear();

    EXPECT_EQ(history.IdBehindNext(1), 0u);
    EXPECT_EQ(history.OldestPending(), 0u);

    history.Queued(7, kT0);
    EXPECT_EQ(history.IdBehindNext(1), 7u);
    EXPECT_EQ(history.IdBehindNext(2), 0u);
}

TEST(PresentHistory, DisplayingAPresentRetiresEveryEarlierOneAndTimesIt)
{
    PresentHistory history;
    history.Queued(1, kT0);
    history.Queued(2, kT0 + milliseconds(7));
    history.Queued(3, kT0 + milliseconds(14));

    const std::optional<double> latency = history.Displayed(2, kT0 + milliseconds(30));
    ASSERT_TRUE(latency.has_value());
    EXPECT_NEAR(*latency, 0.023, 1e-9);
    EXPECT_EQ(history.OldestPending(), 3u);
    EXPECT_EQ(history.PendingCount(), 1u);

    // Already retired: no second timing for the same present.
    EXPECT_FALSE(history.Displayed(2, kT0 + milliseconds(40)).has_value());
    // Retiring presents never forgets which ids the chain queued.
    EXPECT_EQ(history.IdBehindNext(3), 1u);
}

TEST(PresentHistory, OverflowDropsTheOldestTimestampsButKeepsOrder)
{
    PresentHistory history;
    for (std::uint64_t id = 1; id <= 40; ++id)
        history.Queued(id, kT0 + milliseconds(static_cast<int>(id)));

    EXPECT_GT(history.OldestPending(), 1u);
    EXPECT_EQ(history.IdBehindNext(1), 40u);
    EXPECT_EQ(history.IdBehindNext(40), 1u);

    const std::optional<double> latency = history.Displayed(40, kT0 + milliseconds(50));
    ASSERT_TRUE(latency.has_value());
    EXPECT_NEAR(*latency, 0.010, 1e-9);
    EXPECT_EQ(history.PendingCount(), 0u);
}
