#include <gtest/gtest.h>

#include <graphics/PresentHistory.h>

#include <chrono>

namespace
{
    using Clock = PresentHistory::Clock;
    using std::chrono::milliseconds;

    const Clock::time_point kT0 = Clock::time_point{} + std::chrono::hours(1);
    constexpr double kRefreshSeconds = 1.0 / 144.0;

    void Queue(PresentHistory& history, std::uint64_t id, int queuedMs, int frameStartMs)
    {
        history.Queued(id, kT0 + milliseconds(queuedMs), kT0 + milliseconds(frameStartMs));
    }
}

TEST(PresentHistory, DisplayingAPresentRetiresEveryEarlierOneAndTimesIt)
{
    PresentHistory history;
    Queue(history, 1, 2, 0);
    Queue(history, 2, 9, 7);
    Queue(history, 3, 16, 14);
    EXPECT_EQ(history.OldestPending(), 1u);

    const std::optional<PresentHistory::DisplayedPresent> displayed = history.Displayed(2, kT0 + milliseconds(30));
    ASSERT_TRUE(displayed.has_value());
    EXPECT_NEAR(displayed->LatencyFromQueue, 0.021, 1e-9);
    EXPECT_NEAR(displayed->LatencyFromFrameStart, 0.023, 1e-9);
    EXPECT_EQ(history.OldestPending(), 3u);
    EXPECT_EQ(history.PendingCount(), 1u);

    // Already retired: no second timing for the same present.
    EXPECT_FALSE(history.Displayed(2, kT0 + milliseconds(40)).has_value());
    // Retiring never forgets which ids the chain queued.
    const std::optional<PresentHistory::DisplayHold> hold = history.HoldToQueue(2, kRefreshSeconds);
    ASSERT_TRUE(hold.has_value());
    EXPECT_EQ(hold->Awaited, 1u);
}

TEST(PresentHistory, HoldsForThePresentThatLeavesAtMostTheBoundQueued)
{
    PresentHistory history;
    EXPECT_FALSE(history.HoldToQueue(1, kRefreshSeconds).has_value());
    Queue(history, 10, 0, 0);
    Queue(history, 11, 7, 7);
    Queue(history, 12, 14, 14);

    // Once present 10 is displayed, 11 and 12 are all that wait.
    EXPECT_EQ(history.HoldToQueue(2, kRefreshSeconds)->Awaited, 10u);
    EXPECT_EQ(history.HoldToQueue(1, kRefreshSeconds)->Awaited, 11u);
    EXPECT_EQ(history.HoldToQueue(0, kRefreshSeconds)->Awaited, 12u);
    EXPECT_FALSE(history.HoldToQueue(3, kRefreshSeconds).has_value());
    EXPECT_FALSE(history.HoldToQueue(2, 0.0).has_value());
}

// A hidden or locked display stops showing presents; the frame loop must keep
// running, so the hold gives up a refresh after the queue should have drained.
TEST(PresentHistory, TheHoldIsBoundedByTheQueueDrainingPlusOneRefresh)
{
    PresentHistory history;
    for (std::uint64_t id = 1; id <= 10; ++id)
        Queue(history, id, static_cast<int>(id) * 7, static_cast<int>(id) * 7);

    for (const std::uint32_t maxQueued : { 1u, 2u, 3u, 4u })
    {
        const std::optional<PresentHistory::DisplayHold> hold = history.HoldToQueue(maxQueued, kRefreshSeconds);
        ASSERT_TRUE(hold.has_value()) << "bound " << maxQueued;
        const std::chrono::duration<double> expected{ kRefreshSeconds * (maxQueued + 2) };
        EXPECT_NEAR(std::chrono::duration<double>(hold->Timeout).count(), expected.count(), 1e-6)
            << "bound " << maxQueued;
    }
}

TEST(PresentHistory, ARebuiltChainForgetsItsPresents)
{
    PresentHistory history;
    Queue(history, 5, 0, 0);
    Queue(history, 6, 7, 7);
    history.Clear();

    EXPECT_EQ(history.OldestPending(), 0u);
    EXPECT_EQ(history.PendingCount(), 0u);
    EXPECT_FALSE(history.HoldToQueue(0, kRefreshSeconds).has_value());
    EXPECT_FALSE(history.Displayed(6, kT0 + milliseconds(20)).has_value());

    // The new chain's ids continue the old ones, but only its own are awaited.
    Queue(history, 7, 30, 30);
    EXPECT_FALSE(history.HoldToQueue(1, kRefreshSeconds).has_value());
}

TEST(PresentHistory, OverflowDropsTheOldestTimestampsButKeepsOrder)
{
    PresentHistory history;
    for (std::uint64_t id = 1; id <= 40; ++id)
        Queue(history, id, static_cast<int>(id), static_cast<int>(id));

    EXPECT_GT(history.OldestPending(), 1u);

    EXPECT_EQ(history.HoldToQueue(39, kRefreshSeconds)->Awaited, 1u);
    const std::optional<PresentHistory::DisplayedPresent> displayed = history.Displayed(40, kT0 + milliseconds(50));
    ASSERT_TRUE(displayed.has_value());
    EXPECT_NEAR(displayed->LatencyFromQueue, 0.010, 1e-9);
    EXPECT_EQ(history.PendingCount(), 0u);
}
