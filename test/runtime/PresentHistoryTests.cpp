#include <gtest/gtest.h>

#include <graphics/PresentHistory.h>

#include <chrono>

namespace
{
    using Clock = PresentHistory::Clock;
    using std::chrono::milliseconds;

    const Clock::time_point kT0 = Clock::time_point{} + std::chrono::hours(1);

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
    EXPECT_TRUE(history.SeenDisplayed(1));
    EXPECT_TRUE(history.SeenDisplayed(2));
    EXPECT_FALSE(history.SeenDisplayed(3));

    // Already retired: no second timing for the same present.
    EXPECT_FALSE(history.Displayed(2, kT0 + milliseconds(40)).has_value());
}

TEST(PresentHistory, ARebuiltChainForgetsItsPresents)
{
    PresentHistory history;
    Queue(history, 5, 0, 0);
    Queue(history, 6, 7, 7);
    history.Clear();

    EXPECT_EQ(history.OldestPending(), 0u);
    EXPECT_FALSE(history.SeenDisplayed(1));
    EXPECT_FALSE(history.Displayed(6, kT0 + milliseconds(20)).has_value());
}

TEST(PresentHistory, OverflowDropsTheOldestTimestampsButKeepsOrder)
{
    PresentHistory history;
    for (std::uint64_t id = 1; id <= 40; ++id)
        Queue(history, id, static_cast<int>(id), static_cast<int>(id));

    EXPECT_GT(history.OldestPending(), 1u);

    const std::optional<PresentHistory::DisplayedPresent> displayed = history.Displayed(40, kT0 + milliseconds(50));
    ASSERT_TRUE(displayed.has_value());
    EXPECT_NEAR(displayed->LatencyFromQueue, 0.010, 1e-9);
    EXPECT_EQ(history.OldestPending(), 0u);
    EXPECT_TRUE(history.SeenDisplayed(1)) << "a present dropped from the ring is still known displayed";
}
