#include <gtest/gtest.h>

#include <graphics/DisplayQueueLimit.h>
#include <graphics/PresentHistory.h>
#include <graphics/vulkan/VulkanSwapchainService.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    using Clock = DisplayQueueLimit::Clock;
    using Micros = std::chrono::microseconds;

    constexpr std::int64_t kRefreshUs = 6944;  // 144 Hz
    constexpr double kRefreshSeconds = kRefreshUs / 1.0e6;
    const Clock::time_point kT0 = Clock::time_point{} + std::chrono::hours(1);

    Clock::time_point At(std::int64_t us) { return kT0 + Micros(us); }

    DisplayQueueLimit::Observation Shown(std::uint64_t id, std::int64_t us, bool held = true,
                                         double production = 0.0005)
    {
        return { .Id = id, .DisplayedAt = At(us), .HeldForDisplay = held, .ProductionSeconds = production };
    }

    // Feeds presents displayed one refresh apart, with a two-refresh gap before
    // each id in `late`.
    void ShowRun(DisplayQueueLimit& limit, std::uint64_t first, std::uint64_t last, std::int64_t& clockUs,
                 std::vector<std::uint64_t> late = {}, bool held = true, double production = 0.0005)
    {
        for (std::uint64_t id = first; id <= last; ++id)
        {
            clockUs += std::ranges::find(late, id) != late.end() ? 2 * kRefreshUs : kRefreshUs;
            limit.Displayed(Shown(id, clockUs, held, production), kRefreshSeconds);
        }
    }

    struct Settled
    {
        std::uint32_t Depth = 0;
        std::int64_t LongestIntervalUs = 0;
        double FramesPerSecond = 0.0;
    };

    // A FIFO display: a present becomes eligible `latencyUs` after it is queued
    // and is shown at the first refresh after that, one per refresh, in order.
    // The application begins a frame when the previous one is queued and the
    // display queue is within the limit, exactly as the frame service does.
    Settled RunFifoDisplay(std::int64_t latencyUs, std::int64_t productionUs, std::uint32_t initialDepth)
    {
        constexpr std::uint64_t kFrames = 3000;
        constexpr std::uint64_t kSettledFrom = 2000;
        PresentHistory history;
        DisplayQueueLimit limit(initialDepth, PresentHistory::kTracked - 1);
        std::vector<std::int64_t> displayedUs(kFrames + 1, 0);
        std::int64_t previousQueuedUs = 0;

        for (std::uint64_t id = 1; id <= kFrames; ++id)
        {
            std::int64_t startUs = previousQueuedUs;
            bool held = false;
            const std::optional<DisplayQueueLimit::Hold> hold = limit.HoldFor(history, kRefreshSeconds);
            const std::uint64_t awaited = hold ? hold->Awaited : 0;
            if (awaited != 0 && displayedUs[awaited] > startUs)
            {
                startUs = displayedUs[awaited];
                held = true;
                if (const auto shown = history.Displayed(awaited, At(startUs)))
                    limit.Displayed(Shown(awaited, startUs, shown->HeldForDisplay, shown->ProductionSeconds),
                                    kRefreshSeconds);
            }
            while (const std::uint64_t oldest = history.OldestPending())
            {
                if (displayedUs[oldest] > startUs)
                    break;
                (void)history.Displayed(oldest, At(startUs));
            }

            const std::int64_t queuedUs = startUs + productionUs;
            history.Queued(id, At(queuedUs), PresentHistory::Frame{ .StartedAt = At(startUs), .HeldForDisplay = held });
            const std::int64_t eligibleUs = queuedUs + latencyUs;
            std::int64_t shownUs = (eligibleUs + kRefreshUs - 1) / kRefreshUs * kRefreshUs;
            if (id > 1)
                shownUs = std::max(shownUs, displayedUs[id - 1] + kRefreshUs);
            displayedUs[id] = shownUs;
            previousQueuedUs = queuedUs;
        }

        Settled settled{ .Depth = limit.Depth() };
        for (std::uint64_t id = kSettledFrom + 1; id <= kFrames; ++id)
            settled.LongestIntervalUs = std::max(settled.LongestIntervalUs, displayedUs[id] - displayedUs[id - 1]);
        settled.FramesPerSecond = static_cast<double>(kFrames - kSettledFrom) * 1.0e6
            / static_cast<double>(displayedUs[kFrames] - displayedUs[kSettledFrom]);
        return settled;
    }
}

// The old pacing held every frame on the display in every present mode. Only
// a mode that shows one image per refresh may hold a frame for the display.
TEST(DisplayQueueLimit, OnlyDisplayPacedModesHoldAFrame)
{
    EXPECT_TRUE(IsDisplayPaced(VK_PRESENT_MODE_FIFO_KHR));
    EXPECT_TRUE(IsDisplayPaced(VK_PRESENT_MODE_FIFO_RELAXED_KHR));
    EXPECT_FALSE(IsDisplayPaced(VK_PRESENT_MODE_MAILBOX_KHR));
    EXPECT_FALSE(IsDisplayPaced(VK_PRESENT_MODE_IMMEDIATE_KHR));
}

TEST(DisplayQueueLimit, HoldsForThePresentDepthBehindTheNext)
{
    DisplayQueueLimit limit(2, 15);
    PresentHistory history;
    EXPECT_FALSE(limit.HoldFor(history, kRefreshSeconds).has_value());

    history.Queued(1, At(0), {});
    EXPECT_FALSE(limit.HoldFor(history, kRefreshSeconds).has_value());

    history.Queued(2, At(kRefreshUs), {});
    history.Queued(3, At(2 * kRefreshUs), {});
    const std::optional<DisplayQueueLimit::Hold> hold = limit.HoldFor(history, kRefreshSeconds);
    ASSERT_TRUE(hold.has_value());
    EXPECT_EQ(hold->Awaited, 2u);

    EXPECT_FALSE(limit.HoldFor(history, 0.0).has_value());
}

// A hidden or locked display stops showing presents; the frame loop must keep
// running, so the hold gives up a refresh after the queue should have drained.
TEST(DisplayQueueLimit, TheHoldIsBoundedByTheDepthPlusOneRefresh)
{
    PresentHistory history;
    for (std::uint64_t id = 1; id <= 15; ++id)
        history.Queued(id, At(static_cast<std::int64_t>(id) * kRefreshUs), {});

    for (const std::uint32_t depth : { 1u, 2u, 4u, 15u })
    {
        DisplayQueueLimit limit(depth, 15);
        const std::optional<DisplayQueueLimit::Hold> hold = limit.HoldFor(history, kRefreshSeconds);
        ASSERT_TRUE(hold.has_value()) << "depth " << depth;
        const auto expected = std::chrono::duration_cast<std::chrono::nanoseconds>(Micros(kRefreshUs * (depth + 1)));
        EXPECT_NEAR(static_cast<double>(hold->Timeout.count()), static_cast<double>(expected.count()), 1000.0)
            << "depth " << depth;
    }
}

TEST(DisplayQueueLimit, AnIsolatedLateFrameIsAHitchNotADeficit)
{
    DisplayQueueLimit limit(2, 15);
    std::int64_t clockUs = 0;
    ShowRun(limit, 1, 300, clockUs, { 50, 250 });
    EXPECT_EQ(limit.Depth(), 2u);
}

TEST(DisplayQueueLimit, RepeatedStarvationDeepensTheQueueOneStepAtATime)
{
    DisplayQueueLimit limit(2, 15);
    std::int64_t clockUs = 0;
    ShowRun(limit, 1, 20, clockUs, { 5, 8 });
    EXPECT_EQ(limit.Depth(), 3u);
    ShowRun(limit, 21, 40, clockUs, { 25, 28 });
    EXPECT_EQ(limit.Depth(), 4u);
}

TEST(DisplayQueueLimit, LatenessTheLimitDidNotCauseNeverDeepensIt)
{
    std::int64_t clockUs = 0;
    DisplayQueueLimit notHeld(2, 15);
    ShowRun(notHeld, 1, 40, clockUs, { 5, 8, 11, 14 }, /*held=*/false);
    EXPECT_EQ(notHeld.Depth(), 2u);

    DisplayQueueLimit slowFrames(2, 15);
    ShowRun(slowFrames, 1, 40, clockUs, { 5, 8, 11, 14 }, /*held=*/true, /*production=*/kRefreshSeconds * 1.5);
    EXPECT_EQ(slowFrames.Depth(), 2u);
}

TEST(DisplayQueueLimit, OnlyConsecutivePresentsMeasureAnInterval)
{
    DisplayQueueLimit limit(2, 15);
    for (std::uint64_t id = 1; id <= 40; id += 2)
        limit.Displayed(Shown(id, static_cast<std::int64_t>(id) * 2 * kRefreshUs), kRefreshSeconds);
    EXPECT_EQ(limit.Depth(), 2u);
}

TEST(DisplayQueueLimit, NeverPassesItsMaximumAndResetsToItsStart)
{
    DisplayQueueLimit limit(2, 4);
    std::int64_t clockUs = 0;
    std::vector<std::uint64_t> late;
    for (std::uint64_t id = 2; id <= 200; id += 2)
        late.push_back(id);
    ShowRun(limit, 1, 200, clockUs, late);
    EXPECT_EQ(limit.Depth(), 4u);

    limit.Reset();
    EXPECT_EQ(limit.Depth(), 2u);
}

// The invariant the limit exists for: whatever the presentation path's
// latency, once settled the display is fed every refresh, and the queue is no
// deeper than that latency requires.
TEST(DisplayQueueLimit, SettlesAtFullRefreshForAnyPresentationLatency)
{
    constexpr std::int64_t kProductionUs = 500;
    for (const double latencyRefreshes : { 0.4, 1.2, 1.9, 2.8, 3.6, 5.3 })
    {
        const auto latencyUs = static_cast<std::int64_t>(latencyRefreshes * kRefreshUs);
        const Settled settled = RunFifoDisplay(latencyUs, kProductionUs, 2);
        const auto needed = static_cast<std::uint32_t>((latencyUs + kProductionUs + kRefreshUs - 1) / kRefreshUs);

        EXPECT_EQ(settled.LongestIntervalUs, kRefreshUs) << "latency " << latencyRefreshes << " refreshes";
        EXPECT_NEAR(settled.FramesPerSecond, 1.0e6 / kRefreshUs, 0.01) << "latency " << latencyRefreshes;
        EXPECT_EQ(settled.Depth, std::max(needed, 2u)) << "latency " << latencyRefreshes;
    }
}

// A frame that takes longer than a refresh to produce is the application's
// limit, not the display queue's: the queue must not deepen to chase it.
TEST(DisplayQueueLimit, ASlowApplicationDoesNotDeepenTheQueue)
{
    const Settled settled = RunFifoDisplay(2 * kRefreshUs, kRefreshUs * 3 / 2, 2);
    EXPECT_EQ(settled.Depth, 2u);
}
