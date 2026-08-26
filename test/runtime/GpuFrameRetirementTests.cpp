// The fence-anchored frame clock that decides when a released GPU resource is
// safe to destroy.
//
// Three call sites used to answer this independently -- a frame counter plus
// kRetireFrames in ViewportTargetCache, a countdown of four in
// MaterialThumbnailCache, and the deferred-destroy ring's own bucket rotation.
// The countdown was one short of the guarantee at four frames in flight. These
// tests pin the predicate so the remaining single implementation cannot drift.

#include <gtest/gtest.h>

#include <graphics/FramesInFlight.h>
#include <graphics/GpuFrameRetirement.h>

#include <vector>

TEST(GpuFrameRetirement, RetiresNothingBeforeAnyFenceHasBeenWaited)
{
    const GpuFrameRetirement clock{ .Current = 3, .RetiredThrough = 0 };

    // The first frames of a session run with nothing proven complete. A cache
    // that reads this as "safe" would free a resource the GPU is reading.
    EXPECT_FALSE(clock.IsRetired(0));
    EXPECT_FALSE(clock.IsRetired(3));
}

TEST(GpuFrameRetirement, DoesNotRetireTheFrameCurrentlyRecording)
{
    // RetiredThrough == 5 means frames 0..4 are done; frame 5 is not.
    const GpuFrameRetirement clock{ .Current = 5, .RetiredThrough = 5 };

    EXPECT_TRUE(clock.IsRetired(4));
    EXPECT_FALSE(clock.IsRetired(5))
        << "a resource released this frame is still referenced by this frame";
    EXPECT_FALSE(clock.IsRetired(6));
}

TEST(GpuFrameRetirement, StampIsTheFrameBeingRecorded)
{
    const GpuFrameRetirement clock{ .Current = 9, .RetiredThrough = 6 };
    EXPECT_EQ(clock.Stamp(), 9u);
}

TEST(GpuFrameRetirement, AdvanceMovesTheBoundaryPastTheCompletedFrame)
{
    EXPECT_EQ(AdvanceRetiredThrough(0, 0), 1u);
    EXPECT_EQ(AdvanceRetiredThrough(1, 4), 5u);
}

TEST(GpuFrameRetirement, AdvanceNeverMovesTheBoundaryBackwards)
{
    // A slot that never submitted is skipped, so completions can be proven out
    // of order. Regressing the boundary would re-expose a freed resource.
    EXPECT_EQ(AdvanceRetiredThrough(10, 2), 10u);
    EXPECT_EQ(AdvanceRetiredThrough(10, 9), 10u);
    EXPECT_EQ(AdvanceRetiredThrough(10, 10), 11u);
}

// Replays the service's rule: frames are numbered as they begin, slots are used
// round-robin, and waiting a slot's fence proves the frame that last submitted
// on it. A resource released during frame N must survive until the GPU is done
// with frame N, which is at least `framesInFlight` frames later.
TEST(GpuFrameRetirement, SurvivesEveryInFlightFrameAtEveryConfiguredDepth)
{
    for (std::uint32_t framesInFlight = kMinFramesInFlight;
         framesInFlight <= kMaxFramesInFlight;
         ++framesInFlight)
    {
        GpuFrameRetirement clock;
        std::vector<std::uint64_t> slotFrame(framesInFlight, 0);
        std::vector<bool> slotSubmitted(framesInFlight, false);

        constexpr std::uint64_t kReleasedOn = 6;
        std::uint64_t freedOn = 0;
        bool freed = false;

        for (std::uint64_t step = 0; step < 40; ++step)
        {
            const std::size_t slot = step % framesInFlight;

            ++clock.Current;
            if (slotSubmitted[slot])
            {
                clock.RetiredThrough =
                    AdvanceRetiredThrough(clock.RetiredThrough, slotFrame[slot]);
            }

            if (!freed && clock.Current > kReleasedOn
                && clock.IsRetired(kReleasedOn))
            {
                freed = true;
                freedOn = clock.Current;
            }

            slotFrame[slot] = clock.Current;
            slotSubmitted[slot] = true;
        }

        ASSERT_TRUE(freed) << "frames in flight: " << framesInFlight;
        EXPECT_GE(freedOn, kReleasedOn + framesInFlight)
            << "freed too early at " << framesInFlight << " frames in flight";
    }
}

TEST(GpuFrameRetirement, ASkippedSubmissionDoesNotRetireTheSkippedFrame)
{
    // Frame 4 acquires a number but errors out before submitting. Its slot is
    // reused by frame 6, and waiting that slot must not imply frame 5 is done.
    GpuFrameRetirement clock{ .Current = 4, .RetiredThrough = 3 };

    // Nothing proves 4 complete, so the boundary stays where it was.
    EXPECT_FALSE(clock.IsRetired(4));
    EXPECT_TRUE(clock.IsRetired(2));

    // The next real completion is frame 3's slot again, which proves only 3.
    clock.RetiredThrough = AdvanceRetiredThrough(clock.RetiredThrough, 3);
    EXPECT_EQ(clock.RetiredThrough, 4u);
    EXPECT_FALSE(clock.IsRetired(4));
}

TEST(GpuFrameRetirement, ComparesEqualByValue)
{
    // The clock is copied into FrameContext every frame and stored by caches;
    // value semantics are what make that safe.
    const GpuFrameRetirement a{ .Current = 2, .RetiredThrough = 1 };
    const GpuFrameRetirement b{ .Current = 2, .RetiredThrough = 1 };
    const GpuFrameRetirement c{ .Current = 2, .RetiredThrough = 2 };

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
