#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>
#include <graphics/PresentHistory.h>
#include <graphics/vulkan/PresentationFramePlan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using Clock = PresentHistory::Clock;

    constexpr std::int64_t kRefreshUs = 6944;
    constexpr double kRefreshHz = 1.0e6 / kRefreshUs;
    constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();
    constexpr std::uint64_t kFrames = 3000;
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

    // The frame service against a FIFO display, in microseconds. A frame waits
    // for its slot's fence, simulates, then acquires in BeginFrame until the
    // acquire deadline, skipping render and present if no image came back. The
    // display shows one present per refresh and returns an image once
    // `DisplayHolds` later presents have been shown.
    struct Pipeline
    {
        std::uint32_t FramesInFlight = 2;
        std::uint32_t MinImages = 2;
        std::uint32_t DisplayHolds = 1;
        std::int64_t SimulateUs = 500;
        std::int64_t RecordUs = 500;
        std::int64_t GpuUs = 500;
        std::int64_t PathUs = 0;
        // The display shows nothing.
        Span Stall{};
        // Present wait reports presents shown in this span `LateUs` late.
        Span Late{};
        std::int64_t LateUs = 0;
        // Frames starting earlier are the display filling up, and not measured.
        std::int64_t MeasureFromUs = Refreshes(100);
    };

    struct Outcome
    {
        std::vector<std::int64_t> StartUs;
        std::int64_t MaxAcquireWaitUs = 0;
        std::int64_t MaxAcquireToShownUs = 0;
        double MaxLatencyEstimateSeconds = 0.0;
        double PresentsPerSecond = 0.0;
        std::uint64_t FramesSkipped = 0;
        std::uint64_t FramesBegunDuringStall = 0;
    };

    Outcome Simulate(const Pipeline& p)
    {
        const std::uint32_t imageCount = SwapchainImageCount(p.MinImages, 0, p.FramesInFlight);
        std::vector<std::int64_t> imageFreeUs(imageCount, 0);
        std::vector<std::uint32_t> imageOf(kFrames + 1, 0);
        std::vector<std::int64_t> shownUs(kFrames + 1, 0);
        std::vector<std::int64_t> reportedUs(kFrames + 1, 0);
        std::vector<std::int64_t> gpuDoneUs(kFrames + 1, 0);
        PresentHistory history;
        Outcome outcome;
        std::int64_t cpuFreeUs = 0;
        std::uint64_t presents = 0;
        std::uint64_t firstMeasured = 0;

        // Present wait for `id` completes once it or any later present is reported.
        const auto reportedAt = [&](std::uint64_t id) {
            std::int64_t at = kNever;
            for (std::uint64_t later = id; later <= presents; ++later)
                at = std::min(at, reportedUs[later]);
            return at;
        };

        for (std::uint64_t frame = 1; frame <= kFrames; ++frame)
        {
            std::int64_t startUs = cpuFreeUs;
            if (presents >= p.FramesInFlight)
                startUs = std::max(startUs, gpuDoneUs[presents + 1 - p.FramesInFlight]);
            outcome.StartUs.push_back(startUs);
            outcome.FramesBegunDuringStall += p.Stall.Contains(startUs) ? 1 : 0;
            const bool measured = startUs >= p.MeasureFromUs;
            const std::int64_t acquireStartUs = startUs + p.SimulateUs;

            while (const std::uint64_t oldest = history.OldestPending())
            {
                if (reportedAt(oldest) > acquireStartUs)
                    break;
                const auto displayed = history.Displayed(oldest, At(acquireStartUs));
                if (displayed && measured)
                    outcome.MaxLatencyEstimateSeconds = std::max(outcome.MaxLatencyEstimateSeconds,
                                                                 displayed->LatencyFromFrameStart);
            }

            const auto image = std::ranges::min_element(imageFreeUs);
            const AcquireDeadline deadline = AcquireDeadline::ForDisplay(At(acquireStartUs), kRefreshHz);
            const auto timeoutUs = static_cast<std::int64_t>(deadline.TimeoutAt(At(acquireStartUs)) / 1000);
            if (*image > acquireStartUs + timeoutUs)
            {
                cpuFreeUs = acquireStartUs + timeoutUs;
                if (measured)
                {
                    ++outcome.FramesSkipped;
                    outcome.MaxAcquireWaitUs = std::max(outcome.MaxAcquireWaitUs, timeoutUs);
                }
                continue;
            }
            const std::int64_t acquiredUs = std::max(acquireStartUs, *image);
            const auto imageIndex = static_cast<std::uint32_t>(image - imageFreeUs.begin());
            *image = kNever;

            const std::uint64_t id = ++presents;
            imageOf[id] = imageIndex;
            const std::int64_t submittedUs = acquiredUs + p.RecordUs;
            cpuFreeUs = submittedUs;
            gpuDoneUs[id] = std::max(submittedUs, gpuDoneUs[id - 1]) + p.GpuUs;
            std::int64_t shown = NextRefreshAtOrAfter(gpuDoneUs[id] + p.PathUs);
            if (id > 1)
                shown = std::max(shown, shownUs[id - 1] + kRefreshUs);
            if (p.Stall.Contains(shown))
                shown = NextRefreshAtOrAfter(p.Stall.FromUs + p.Stall.Us);
            shownUs[id] = shown;
            reportedUs[id] = shown + (p.Late.Contains(shown) ? p.LateUs : 0);
            if (id > p.DisplayHolds)
                imageFreeUs[imageOf[id - p.DisplayHolds]] = shown;
            history.Queued(id, At(submittedUs), At(startUs));

            if (!measured)
                continue;
            firstMeasured = firstMeasured == 0 ? id : firstMeasured;
            outcome.MaxAcquireWaitUs = std::max(outcome.MaxAcquireWaitUs, acquiredUs - acquireStartUs);
            outcome.MaxAcquireToShownUs = std::max(outcome.MaxAcquireToShownUs, shown - acquiredUs);
        }
        outcome.PresentsPerSecond = static_cast<double>(presents - firstMeasured) * 1.0e6
            / static_cast<double>(shownUs[presents] - shownUs[firstMeasured]);
        return outcome;
    }

    struct Work
    {
        double Record = 0.0;
        double Gpu = 0.0;
        double Path = 0.0;
    };
}

// Present wait only estimates latency. Its completions arriving late change
// that estimate and nothing else: every frame begins when it would have with
// prompt completions.
TEST(DisplayQueue, ALatePresentWaitCompletionDelaysNoFrame)
{
    const Pipeline prompt{ .PathUs = Refreshes(0.9) };
    Pipeline late = prompt;
    late.Late = { .FromUs = 2'000'000, .Us = 50'000 };
    late.LateUs = 60'000;

    const Outcome promptRun = Simulate(prompt);
    const Outcome lateRun = Simulate(late);
    EXPECT_EQ(lateRun.StartUs, promptRun.StartUs);
    EXPECT_EQ(lateRun.MaxAcquireWaitUs, promptRun.MaxAcquireWaitUs);
    EXPECT_GT(lateRun.MaxLatencyEstimateSeconds, promptRun.MaxLatencyEstimateSeconds + 0.04);
}

// Within a refresh of work per stage the display is fed every refresh, and a
// present is shown at most one refresh per other image after its acquire,
// plus its own work.
TEST(DisplayQueue, WithinTheBudgetEveryRefreshIsFedFromTheImages)
{
    for (std::uint32_t frames = kMinFramesInFlight; frames <= kMaxFramesInFlight; ++frames)
    {
        for (const Work work : { Work{ 0.07, 0.07, 0.0 }, Work{ 0.07, 0.07, 0.9 }, Work{ 0.45, 0.45, 0.0 },
                                 Work{ 0.45, 0.45, 0.9 } })
        {
            for (const std::uint32_t minImages : { 2u, 3u })
            {
                const Pipeline p{ .FramesInFlight = frames, .MinImages = minImages, .RecordUs = Refreshes(work.Record),
                                  .GpuUs = Refreshes(work.Gpu), .PathUs = Refreshes(work.Path) };
                const Outcome run = Simulate(p);
                const std::string cell = "frames " + std::to_string(frames) + " record " + std::to_string(work.Record)
                    + " gpu " + std::to_string(work.Gpu) + " path " + std::to_string(work.Path) + " min images "
                    + std::to_string(minImages);
                const std::int64_t images = SwapchainImageCount(minImages, 0, frames);
                EXPECT_NEAR(run.PresentsPerSecond, kRefreshHz, 0.01) << cell;
                EXPECT_LE(run.MaxAcquireToShownUs, (images - 1) * kRefreshUs + p.RecordUs + p.GpuUs + p.PathUs)
                    << cell;
            }
        }
    }
}

// A display that returns no image costs each frame at most the acquire
// deadline: the loop keeps simulating through the stall, and once the display
// resumes every refresh is fed again from the images.
TEST(DisplayQueue, ADisplayThatReturnsNoImageCostsAFrameAtMostTheDeadline)
{
    const Span stall{ .FromUs = 2'000'000, .Us = 100'000 };
    const std::int64_t deadlineUs = Refreshes(kAcquireDeadlineRefreshes);
    const Pipeline stalled{ .PathUs = Refreshes(0.9), .Stall = stall };
    const Outcome run = Simulate(stalled);
    EXPECT_LE(run.MaxAcquireWaitUs, deadlineUs + 1);
    EXPECT_GT(run.FramesSkipped, 0u);
    EXPECT_GE(run.FramesBegunDuringStall, static_cast<std::uint64_t>(stall.Us / (deadlineUs + stalled.SimulateUs)));

    Pipeline resumed = stalled;
    resumed.MeasureFromUs = stall.FromUs + stall.Us + Refreshes(10);
    const Outcome after = Simulate(resumed);
    EXPECT_NEAR(after.PresentsPerSecond, kRefreshHz, 0.01);
    EXPECT_EQ(after.FramesSkipped, 0u);
}
