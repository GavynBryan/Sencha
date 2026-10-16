#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>
#include <graphics/PresentHistory.h>
#include <graphics/vulkan/VulkanSwapchainService.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using Clock = PresentHistory::Clock;

    constexpr std::int64_t kRefreshUs = 6944;
    constexpr double kRefreshSeconds = kRefreshUs / 1.0e6;
    constexpr double kRefreshHz = 1.0e6 / kRefreshUs;
    constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();
    constexpr std::uint64_t kFrames = 3000;
    // Every present further back than this has been shown.
    constexpr std::uint64_t kLookBack = 32;
    const Clock::time_point kT0 = Clock::time_point{} + std::chrono::hours(1);

    Clock::time_point At(std::int64_t us) { return kT0 + std::chrono::microseconds(us); }
    std::int64_t NextRefreshAtOrAfter(std::int64_t us) { return (us + kRefreshUs - 1) / kRefreshUs * kRefreshUs; }
    std::int64_t Refreshes(double count) { return static_cast<std::int64_t>(count * kRefreshUs); }

    struct Span
    {
        std::int64_t FromUs = 0;
        std::int64_t Us = 0;
        [[nodiscard]] bool Contains(std::int64_t us) const { return Us > 0 && us >= FromUs && us < FromUs + Us; }
    };

    // The frame service against a FIFO display, in microseconds: frames wait as
    // AwaitFrameSlot does and acquire as BeginFrame does; the display shows one
    // present per refresh and frees the image it showed before.
    struct Pipeline
    {
        std::uint32_t FramesInFlight = 2;
        std::uint32_t MinImages = 2;
        bool Held = true;
        std::int64_t CpuUs = 500;
        std::int64_t GpuUs = 500;
        std::int64_t PathUs = 0;
        Span Stall{};
        // Presents consumed but never reported displayed.
        Span Hidden{};
    };

    struct Outcome
    {
        std::size_t MaxQueuedAtStart = 0;
        std::size_t MaxQueuedAtAcquire = 0;
        std::int64_t MaxAcquireWaitUs = 0;
        std::int64_t MaxLatencyUs = 0;
        std::int64_t MaxLatencyFromAcquireUs = 0;
        std::uint64_t FramesBegunWhileHidden = 0;
        double FramesPerSecond = 0.0;
    };

    // Before the display shows its first frame nothing occupies it, so a
    // chain's first presents can queue one deeper than the steady state.
    constexpr std::uint64_t kDisplayStarted = 100;

    Outcome Simulate(const Pipeline& p, std::uint64_t settledFrom = kDisplayStarted)
    {
        const std::uint32_t maxQueued = MaxQueuedPresents(p.FramesInFlight);
        std::vector<std::int64_t> imageFreeUs(SwapchainImageCount(p.MinImages, 0, p.FramesInFlight), 0);
        std::vector<std::size_t> imageOf(kFrames + 1, 0);
        std::vector<std::int64_t> gpuDoneUs(kFrames + 1, 0);
        std::vector<std::int64_t> shownUs(kFrames + 1, 0);
        std::vector<std::int64_t> reportedUs(kFrames + 1, 0);
        PresentHistory history;
        Outcome outcome;
        std::int64_t cpuFreeUs = 0;

        // Present wait for `id` completes when it or any later present is reported.
        const auto presentedAt = [&](std::uint64_t id, std::uint64_t newest) {
            std::int64_t at = kNever;
            for (std::uint64_t later = id; later <= newest; ++later)
                at = std::min(at, reportedUs[later]);
            return at;
        };
        const auto queuedAt = [&](std::uint64_t id, std::int64_t us) {
            std::size_t queued = 0;
            for (std::uint64_t earlier = id > kLookBack ? id - kLookBack : 1; earlier < id; ++earlier)
                queued += shownUs[earlier] > us ? 1 : 0;
            return queued;
        };

        for (std::uint64_t id = 1; id <= kFrames; ++id)
        {
            std::int64_t startUs = cpuFreeUs;
            if (id > p.FramesInFlight)
                startUs = std::max(startUs, gpuDoneUs[id - p.FramesInFlight]);
            if (p.Held)
            {
                if (const std::optional<PresentHistory::DisplayHold> hold = history.HoldToQueue(maxQueued, kRefreshSeconds))
                {
                    const auto timeoutUs = std::chrono::duration_cast<std::chrono::microseconds>(hold->Timeout).count();
                    const std::int64_t presented = presentedAt(hold->Awaited, id - 1);
                    if (presented > startUs)
                        startUs = std::min(presented, startUs + timeoutUs);
                }
            }
            while (const std::uint64_t oldest = history.OldestPending())
            {
                if (presentedAt(oldest, id - 1) > startUs)
                    break;
                (void)history.Displayed(oldest, At(startUs));
            }
            const std::size_t queuedAtStart = history.PendingCount();

            const auto image = std::ranges::min_element(imageFreeUs);
            const std::int64_t acquiredUs = std::max(startUs, *image);
            imageOf[id] = static_cast<std::size_t>(image - imageFreeUs.begin());
            *image = kNever;

            const std::int64_t submittedUs = acquiredUs + p.CpuUs;
            cpuFreeUs = submittedUs;
            gpuDoneUs[id] = std::max(submittedUs, gpuDoneUs[id - 1]) + p.GpuUs;
            std::int64_t shown = NextRefreshAtOrAfter(gpuDoneUs[id] + p.PathUs);
            if (id > 1)
                shown = std::max(shown, shownUs[id - 1] + kRefreshUs);
            if (p.Stall.Contains(shown))
                shown = NextRefreshAtOrAfter(p.Stall.FromUs + p.Stall.Us);
            shownUs[id] = shown;
            reportedUs[id] = p.Hidden.Contains(shown) ? kNever : shown;
            if (id > 1)
                imageFreeUs[imageOf[id - 1]] = shown;
            history.Queued(id, At(submittedUs), At(startUs));

            outcome.FramesBegunWhileHidden += p.Hidden.Contains(startUs) ? 1 : 0;
            if (id < settledFrom)
                continue;
            outcome.MaxQueuedAtStart = std::max(outcome.MaxQueuedAtStart, queuedAtStart);
            outcome.MaxQueuedAtAcquire = std::max(outcome.MaxQueuedAtAcquire, queuedAt(id, acquiredUs));
            outcome.MaxAcquireWaitUs = std::max(outcome.MaxAcquireWaitUs, acquiredUs - startUs);
            outcome.MaxLatencyUs = std::max(outcome.MaxLatencyUs, shown - startUs);
            outcome.MaxLatencyFromAcquireUs = std::max(outcome.MaxLatencyFromAcquireUs, shown - acquiredUs);
        }
        outcome.FramesPerSecond = static_cast<double>(kFrames - settledFrom) * 1.0e6
            / static_cast<double>(shownUs[kFrames] - shownUs[settledFrom]);
        return outcome;
    }

    struct Work
    {
        double Cpu = 0.0;
        double Gpu = 0.0;
        double Path = 0.0;
    };
}

TEST(DisplayQueue, OnlyDisplayPacedModesHoldAFrame)
{
    EXPECT_TRUE(IsDisplayPaced(VK_PRESENT_MODE_FIFO_KHR));
    EXPECT_TRUE(IsDisplayPaced(VK_PRESENT_MODE_FIFO_RELAXED_KHR));
    EXPECT_FALSE(IsDisplayPaced(VK_PRESENT_MODE_MAILBOX_KHR));
    EXPECT_FALSE(IsDisplayPaced(VK_PRESENT_MODE_IMMEDIATE_KHR));
}

// Within the budget, a refresh of CPU and GPU work per frame in flight and one
// for the path, every refresh is fed, a frame begins with at most F presents
// queued and is shown F + 1 refreshes later, and acquire never waits.
TEST(DisplayQueue, WithinTheBudgetEveryRefreshIsFedFromABoundedQueue)
{
    for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
    {
        const double perStage = frames == 1 ? 0.45 : 0.9;
        for (const Work work : { Work{ 0.07, 0.07, 0.0 }, Work{ 0.07, 0.07, 0.9 }, Work{ perStage, perStage, 0.0 },
                                 Work{ perStage, perStage, 0.9 } })
        {
            for (const std::uint32_t minImages : { 2u, 3u })
            {
                const Outcome run = Simulate({ .FramesInFlight = frames, .MinImages = minImages, .CpuUs = Refreshes(work.Cpu),
                                          .GpuUs = Refreshes(work.Gpu), .PathUs = Refreshes(work.Path) });
                const std::string cell = "frames " + std::to_string(frames) + " cpu " + std::to_string(work.Cpu)
                    + " gpu " + std::to_string(work.Gpu) + " path " + std::to_string(work.Path) + " min images "
                    + std::to_string(minImages);
                EXPECT_NEAR(run.FramesPerSecond, kRefreshHz, 0.01) << cell;
                EXPECT_LE(run.MaxQueuedAtStart, frames) << cell;
                EXPECT_LE(run.MaxLatencyUs, (frames + 1) * kRefreshUs) << cell;
                EXPECT_EQ(run.MaxAcquireWaitUs, 0) << cell;
            }
        }
    }
}

// Without present wait, or for a presentation the bound does not hold, acquire
// is what waits, and the image count gives it the same bound. The wait then
// comes after input, which ages by it.
TEST(DisplayQueue, WithoutTheHoldTheImagesBoundTheQueueToTheSameDepth)
{
    for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
    {
        const Outcome run = Simulate({ .FramesInFlight = frames, .Held = false, .PathUs = Refreshes(0.9) });
        EXPECT_NEAR(run.FramesPerSecond, kRefreshHz, 0.01) << "frames " << frames;
        EXPECT_LE(run.MaxQueuedAtAcquire, frames) << "frames " << frames;
        EXPECT_LE(run.MaxLatencyFromAcquireUs, (frames + 1) * kRefreshUs) << "frames " << frames;
        EXPECT_GT(run.MaxAcquireWaitUs, 0) << "frames " << frames;
    }
}

// A path slower than the budget: each frame needs four refreshes from its
// start to the display and three fit in that span, so the display gets three
// frames every four refreshes. The queue does not grow to buy the fourth.
TEST(DisplayQueue, ASlowPresentationPathCostsThroughputNotLatency)
{
    const Pipeline slow{ .PathUs = Refreshes(3.3) };
    const Outcome run = Simulate(slow);
    EXPECT_NEAR(run.FramesPerSecond, kRefreshHz * 3.0 / 4.0, 0.05);
    EXPECT_LE(run.MaxQueuedAtStart, 2u);
    EXPECT_LE(run.MaxLatencyUs, slow.CpuUs + slow.GpuUs + slow.PathUs + kRefreshUs);
}

// Once a compositor that stopped showing frames resumes, the queue drains back
// to its bound and latency to its budget instead of keeping the backlog.
TEST(DisplayQueue, AfterADisplayStallTheQueueReturnsToItsBound)
{
    const Pipeline stalled{ .PathUs = Refreshes(0.9), .Stall = { .FromUs = 2'000'000, .Us = 100'000 } };
    const Outcome run = Simulate(stalled, 2000);
    EXPECT_NEAR(run.FramesPerSecond, kRefreshHz, 0.01);
    EXPECT_LE(run.MaxQueuedAtStart, 2u);
    EXPECT_LE(run.MaxLatencyUs, 3 * kRefreshUs);
}

// A hidden or locked window's presents may never be reported displayed. The
// hold gives up after the queue should have drained, so the loop keeps
// running, and the bound holds again once the window is shown.
TEST(DisplayQueue, AHiddenDisplayCannotHoldTheLoop)
{
    const Span hidden{ .FromUs = 2'000'000, .Us = 1'000'000 };
    const Pipeline p{ .Hidden = hidden };
    const Outcome run = Simulate(p, 2000);
    const std::int64_t longestHoldUs = (MaxQueuedPresents(p.FramesInFlight) + 2) * kRefreshUs;
    EXPECT_GE(run.FramesBegunWhileHidden, static_cast<std::uint64_t>(hidden.Us / longestHoldUs) - 1);
    EXPECT_NEAR(run.FramesPerSecond, kRefreshHz, 0.01);
    EXPECT_LE(run.MaxQueuedAtStart, 2u);
    EXPECT_LE(run.MaxLatencyUs, 3 * kRefreshUs);
}
