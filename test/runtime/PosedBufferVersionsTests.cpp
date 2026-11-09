#include <gtest/gtest.h>

#include <render/PosedBufferVersions.h>

#include <deque>
#include <optional>
#include <random>
#include <set>
#include <vector>

namespace
{
// One frame the simulated GPU has not finished: the version it writes or reads.
struct InFlightFrame
{
    std::uint64_t Number = 0;
    std::uint32_t Version = 0;
};

class SimulatedGpu
{
public:
    explicit SimulatedGpu(std::uint32_t framesInFlight)
        : FramesInFlight(framesInFlight)
    {
    }

    // Starts a frame. Like the frame service, recording waits for the fence of
    // the frame that last used this slot, and only that frame is proven done.
    GpuFrameRetirement BeginFrame()
    {
        ++Clock.Current;
        if (Clock.Current > FramesInFlight)
            Clock.RetiredThrough = AdvanceRetiredThrough(Clock.RetiredThrough, Clock.Current - FramesInFlight);
        while (!Unfinished.empty() && Unfinished.front().Number + FramesInFlight <= Clock.Current)
            Unfinished.pop_front();
        return Clock;
    }

    // The GPU may finish frames early, which must never change what is safe.
    void MaybeFinishEarly(std::mt19937& random)
    {
        while (!Unfinished.empty() && random() % 2 == 0)
            Unfinished.pop_front();
    }

    [[nodiscard]] bool IsReferenced(std::uint32_t version) const
    {
        for (const InFlightFrame& frame : Unfinished)
            if (frame.Version == version)
                return true;
        return false;
    }

    void Submit(std::uint32_t version) { Unfinished.push_back({ Clock.Current, version }); }

private:
    std::uint32_t FramesInFlight = 1;
    GpuFrameRetirement Clock;
    std::deque<InFlightFrame> Unfinished;
};

void SimulatePoseFrames(std::uint32_t framesInFlight, unsigned seed, bool earlyCompletion)
{
    std::mt19937 random(seed);
    SimulatedGpu gpu(framesInFlight);
    PosedBufferVersions versions;
    std::vector<std::uint64_t> content(framesInFlight, 0);
    std::uint64_t newestPose = 0;

    for (int frame = 0; frame < 2000; ++frame)
    {
        const GpuFrameRetirement clock = gpu.BeginFrame();
        if (earlyCompletion)
            gpu.MaybeFinishEarly(random);

        const bool wantsDispatch = !versions.HasPose() || random() % 3 != 0;
        if (wantsDispatch)
        {
            const std::optional<std::uint32_t> target = versions.FindWritable(clock, framesInFlight);
            ASSERT_TRUE(target.has_value()) << "F=" << framesInFlight << " frame " << frame;
            ASSERT_FALSE(gpu.IsReferenced(*target))
                << "wrote a version an unfinished frame reads, F=" << framesInFlight << " frame " << frame;
            content[*target] = ++newestPose;
            versions.MarkWritten(*target, clock);
            gpu.Submit(*target);
        }
        else
        {
            const std::optional<std::uint32_t> latest = versions.ReadLatest(clock);
            ASSERT_TRUE(latest.has_value());
            ASSERT_EQ(content[*latest], newestPose) << "a held pose went stale in storage";
            gpu.Submit(*latest);
        }
    }
}
}

TEST(PosedBufferVersions, ASafeVersionExistsAtEveryFramesInFlight)
{
    for (std::uint32_t framesInFlight = kMinFramesInFlight; framesInFlight <= kMaxFramesInFlight; ++framesInFlight)
        for (unsigned seed = 1; seed <= 16; ++seed)
            SimulatePoseFrames(framesInFlight, seed, false);
}

TEST(PosedBufferVersions, EarlyGpuCompletionNeverChangesWhatIsSafe)
{
    for (std::uint32_t framesInFlight = kMinFramesInFlight; framesInFlight <= kMaxFramesInFlight; ++framesInFlight)
        for (unsigned seed = 1; seed <= 16; ++seed)
            SimulatePoseFrames(framesInFlight, seed, true);
}

TEST(PosedBufferVersions, BeforeAnyPoseThereIsNothingToRead)
{
    PosedBufferVersions versions;
    GpuFrameRetirement clock;
    clock.Current = 1;

    EXPECT_FALSE(versions.HasPose());
    EXPECT_FALSE(versions.ReadLatest(clock).has_value());
}

TEST(PosedBufferVersions, HoldingAPoseKeepsItsVersionFromBeingRewritten)
{
    PosedBufferVersions versions;
    GpuFrameRetirement clock{ .Current = 10, .RetiredThrough = 8 };
    versions.MarkWritten(0, clock);
    clock.Current = 11;
    (void)versions.ReadLatest(clock);
    clock.Current = 12;
    (void)versions.ReadLatest(clock);
    clock = { .Current = 13, .RetiredThrough = 11 };
    versions.MarkWritten(1, clock);

    // Frame 12 may still be reading version 0, which frame 10 last wrote.
    clock = { .Current = 14, .RetiredThrough = 12 };
    EXPECT_EQ(versions.FindWritable(clock, 3), std::optional<std::uint32_t>(2u));
}

TEST(PosedBufferVersions, WithOneFrameInFlightTheOnlyVersionIsRewritten)
{
    PosedBufferVersions versions;
    GpuFrameRetirement clock{ .Current = 3, .RetiredThrough = 3 };
    versions.MarkWritten(0, clock);

    clock = { .Current = 4, .RetiredThrough = 4 };
    EXPECT_EQ(versions.FindWritable(clock, 1), std::optional<std::uint32_t>(0u));
}

TEST(PosedBufferVersions, WhenNothingIsRetiredThereIsNoSafeWrite)
{
    PosedBufferVersions versions;
    GpuFrameRetirement clock{ .Current = 5, .RetiredThrough = 2 };
    versions.MarkWritten(0, clock);
    clock.Current = 6;
    versions.MarkWritten(1, clock);

    clock.Current = 7;
    EXPECT_FALSE(versions.FindWritable(clock, 2).has_value());
}
