// The offset arithmetic that decides whether a frame renders. The cases that
// matter are the boundaries: an instance stream one element past what the
// slice holds, and a stream that cannot fit whole at all.

#include <graphics/FrameScratchRing.h>
#include <graphics/GpuFrameScratch.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace
{
    // The forward pass's real shapes: a frame uniform block, then one
    // 80-byte instance per visible object.
    constexpr std::uint64_t kUniformBytes = 5712;
    constexpr std::uint64_t kInstanceStride = 80;
    constexpr std::uint64_t kMiB = 1024 * 1024;
}

TEST(FrameScratchRing, AllocationsBumpTheCursorAndTrackHighWater)
{
    FrameScratchRing ring(2, 1024);

    const FrameScratchRing::Grant first = ring.Allocate(100, 16);
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(first.Offset, 0u);
    EXPECT_EQ(first.Bytes, 100u);

    // The next allocation starts at the aligned cursor, not at 100.
    const FrameScratchRing::Grant second = ring.Allocate(16, 16);
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(second.Offset, 112u);

    EXPECT_EQ(ring.GetUsedBytes(), 128u);
    EXPECT_EQ(ring.GetHighWaterBytes(), 128u);
    EXPECT_EQ(ring.GetFailedAllocationCount(), 0u);
}

TEST(FrameScratchRing, EachFrameGetsItsOwnSliceAndACleanCursor)
{
    FrameScratchRing ring(2, 1024);

    ASSERT_TRUE(ring.Allocate(512, 16).IsValid());
    EXPECT_EQ(ring.GetUsedBytes(), 512u);

    ring.BeginFrame();
    EXPECT_EQ(ring.GetUsedBytes(), 0u);
    // Frame 1's slice starts one full slice into the ring, so an in-flight
    // frame 0 is not overwritten.
    const FrameScratchRing::Grant next = ring.Allocate(16, 16);
    ASSERT_TRUE(next.IsValid());
    EXPECT_EQ(next.Offset, 1024u);

    // High water spans frames; per-frame use does not.
    EXPECT_EQ(ring.GetHighWaterBytes(), 512u);
}

TEST(FrameScratchRing, FailuresAreCountedAndResetWithTheFrame)
{
    FrameScratchRing ring(2, 1024);

    EXPECT_FALSE(ring.Allocate(2048, 16).IsValid());
    EXPECT_EQ(ring.GetFailedAllocationCount(), 1u);
    // A rejected request must not consume the slice.
    EXPECT_EQ(ring.GetUsedBytes(), 0u);
    EXPECT_TRUE(ring.Allocate(1024, 16).IsValid());

    ring.BeginFrame();
    EXPECT_EQ(ring.GetFailedAllocationCount(), 0u);
}

TEST(FrameScratchRing, ElementGrantsShrinkToWhatTheSliceHolds)
{
    // 1024 bytes with 100 already taken leaves room for 11 elements of 80.
    FrameScratchRing ring(1, 1024);
    ASSERT_TRUE(ring.Allocate(100, 16).IsValid());

    const FrameScratchRing::Grant partial = ring.AllocateElements(50, kInstanceStride, 16);
    ASSERT_TRUE(partial.IsValid());
    EXPECT_EQ(partial.Bytes / kInstanceStride, 11u);
    EXPECT_EQ(partial.Offset, 112u);
    // A partial grant is a grant, not a failure: the caller draws it and asks
    // again.
    EXPECT_EQ(ring.GetFailedAllocationCount(), 0u);

    // Now the slice is genuinely full and the next request gets nothing.
    const FrameScratchRing::Grant none = ring.AllocateElements(50, kInstanceStride, 16);
    EXPECT_FALSE(none.IsValid());
    EXPECT_EQ(ring.GetFailedAllocationCount(), 1u);
}

TEST(FrameScratchRing, AStreamTooLargeForTheSliceIsServedInPartAndAccountedFor)
{
    // Runs all come out of one slice, so chunking does not raise a frame's
    // total capacity: it converts "render nothing" into "render what fits and
    // count the rest". Capacity itself is a sizing decision, which the
    // shortfall reported here is what informs.
    FrameScratchRing ring(1, 4096);

    std::uint64_t remaining = 500;
    std::uint64_t served = 0;
    while (remaining > 0)
    {
        const FrameScratchRing::Grant run =
            ring.AllocateElements(remaining, kInstanceStride, 16);
        if (!run.IsValid())
            break;
        const std::uint64_t count = run.Bytes / kInstanceStride;
        served += count;
        remaining -= count;
    }

    // 4096 / 80 = 51 instances fit; the other 449 are the counted remainder.
    EXPECT_EQ(served, 51u);
    EXPECT_EQ(remaining, 449u);
    EXPECT_EQ(served + remaining, 500u);
}

TEST(FrameScratchRing, TheOneInstanceOverBoundaryStillRendersWhatFits)
{
    // The measured cliff: at the default 1 MiB slice, 13,035 instances fit
    // beside the frame uniforms and 13,036 do not. All-or-nothing made the
    // second case render nothing at all.
    const std::uint64_t exactFit = (kMiB - kUniformBytes) / kInstanceStride;
    ASSERT_EQ(exactFit, 13035u);

    for (std::uint64_t requested : { exactFit, exactFit + 1 })
    {
        FrameScratchRing ring(1, kMiB);
        ASSERT_TRUE(ring.Allocate(kUniformBytes, 256).IsValid());

        const FrameScratchRing::Grant run =
            ring.AllocateElements(requested, kInstanceStride, 16);
        ASSERT_TRUE(run.IsValid()) << requested << " instances";
        EXPECT_GE(run.Bytes / kInstanceStride, exactFit - 1);
    }
}

// One scratch slice serves skinning, shadows, editor views, and the forward
// pass, consumed in feature order. The aggregate counters say the budget ran
// out; these are what say which consumer took it.

TEST(ScratchTagCounters, AttributesGrantsAndFailuresPerConsumer)
{
    ScratchTagCounters counters;
    counters.RecordGrant(ScratchTag::SkinningPalettes, 512);
    counters.RecordGrant(ScratchTag::ForwardInstanceData, 128);
    counters.RecordFailure(ScratchTag::ForwardViewUniforms);

    EXPECT_EQ(counters.UsedBytes(ScratchTag::SkinningPalettes), 512u);
    EXPECT_EQ(counters.UsedBytes(ScratchTag::ForwardInstanceData), 128u);
    EXPECT_EQ(counters.UsedBytes(ScratchTag::ShadowViewUniforms), 0u);
    EXPECT_EQ(counters.FailedAllocations(ScratchTag::ForwardViewUniforms), 1u);
    EXPECT_EQ(counters.FailedAllocations(ScratchTag::SkinningPalettes), 0u);
}

TEST(ScratchTagCounters, GrantsAccumulateWithinAFrame)
{
    ScratchTagCounters counters;
    counters.RecordGrant(ScratchTag::ImmediateVertices, 100);
    counters.RecordGrant(ScratchTag::ImmediateVertices, 250);
    EXPECT_EQ(counters.UsedBytes(ScratchTag::ImmediateVertices), 350u);
}

TEST(ScratchTagCounters, BeginFrameResetsUseButKeepsTheHighWaterMark)
{
    // Sizing a slice wants the peak across the session; diagnosing a frame
    // wants that frame alone.
    ScratchTagCounters counters;
    counters.RecordGrant(ScratchTag::ShadowInstanceTransforms, 4096);
    counters.RecordFailure(ScratchTag::ShadowInstanceTransforms);

    counters.BeginFrame();
    EXPECT_EQ(counters.UsedBytes(ScratchTag::ShadowInstanceTransforms), 0u);
    EXPECT_EQ(counters.FailedAllocations(ScratchTag::ShadowInstanceTransforms), 0u);
    EXPECT_EQ(counters.HighWaterBytes(ScratchTag::ShadowInstanceTransforms), 4096u);

    counters.RecordGrant(ScratchTag::ShadowInstanceTransforms, 1024);
    EXPECT_EQ(counters.HighWaterBytes(ScratchTag::ShadowInstanceTransforms), 4096u);
}

TEST(ScratchTagCounters, EveryTagHasAStableName)
{
    for (std::size_t i = 0; i < ScratchTagCounters::kTagCount; ++i)
    {
        const std::string_view name = ToString(static_cast<ScratchTag>(i));
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "unknown");
    }
}
