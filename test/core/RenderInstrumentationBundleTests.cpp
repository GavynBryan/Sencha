#include <gtest/gtest.h>

#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

// These cover the tier policy the mode latch applies every frame. They are
// intentionally unguarded by SENCHA_ENABLE_RENDER_PROFILING: ResolveInstrumentationBundle
// is compiled in every build, and the Off-path guarantee must hold in the
// shipping build too. GpuTimestampPool and RenderCapture are incomplete types
// here (their headers are excluded when profiling is compiled out), so the
// candidate pointers are non-null sentinels that the function only compares,
// never dereferences.
namespace
{
    GpuTimestampPool* const kGpuSentinel =
        reinterpret_cast<GpuTimestampPool*>(0x10);
    RenderCapture* const kCaptureSentinel =
        reinterpret_cast<RenderCapture*>(0x20);
}

TEST(RenderInstrumentationBundle, OffTierYieldsAllNullSoTheOffPathCannotWrite)
{
    RenderStats stats;
    RenderStatsHistory history;

    const RenderInstrumentation bundle = ResolveInstrumentationBundle(
        RenderProfileMode::Off, &stats, &history, kGpuSentinel, kCaptureSentinel);

    // Every member null is what makes PushRenderStatsFrame and Capture::Append
    // no-op while the mode is Off: there is nothing for them to write through.
    EXPECT_EQ(bundle.Stats, nullptr);
    EXPECT_EQ(bundle.StatsHistory, nullptr);
    EXPECT_EQ(bundle.GpuTimestamps, nullptr);
    EXPECT_EQ(bundle.Capture, nullptr);
}

TEST(RenderInstrumentationBundle, CountersTierEnablesStatsAndHistoryOnly)
{
    RenderStats stats;
    RenderStatsHistory history;

    const RenderInstrumentation bundle = ResolveInstrumentationBundle(
        RenderProfileMode::Counters, &stats, &history, kGpuSentinel,
        kCaptureSentinel);

    EXPECT_EQ(bundle.Stats, &stats);
    EXPECT_EQ(bundle.StatsHistory, &history);
    EXPECT_EQ(bundle.GpuTimestamps, nullptr);
    EXPECT_EQ(bundle.Capture, nullptr);
}

TEST(RenderInstrumentationBundle, GpuTierAddsTimestampsButNotCapture)
{
    RenderStats stats;
    RenderStatsHistory history;

    const RenderInstrumentation bundle = ResolveInstrumentationBundle(
        RenderProfileMode::Gpu, &stats, &history, kGpuSentinel, kCaptureSentinel);

    EXPECT_EQ(bundle.Stats, &stats);
    EXPECT_EQ(bundle.StatsHistory, &history);
    EXPECT_EQ(bundle.GpuTimestamps, kGpuSentinel);
    EXPECT_EQ(bundle.Capture, nullptr);
}

TEST(RenderInstrumentationBundle, CaptureTierEnablesEveryMember)
{
    RenderStats stats;
    RenderStatsHistory history;

    const RenderInstrumentation bundle = ResolveInstrumentationBundle(
        RenderProfileMode::Capture, &stats, &history, kGpuSentinel,
        kCaptureSentinel);

    EXPECT_EQ(bundle.Stats, &stats);
    EXPECT_EQ(bundle.StatsHistory, &history);
    EXPECT_EQ(bundle.GpuTimestamps, kGpuSentinel);
    EXPECT_EQ(bundle.Capture, kCaptureSentinel);
}

TEST(RenderInstrumentationBundle, NullCandidateStaysNullAtItsTier)
{
    RenderStats stats;
    RenderStatsHistory history;

    // A device without timestamp support (or a profiling-compiled-out build)
    // passes a null pool even at Gpu tier; the member must stay null rather
    // than promote the sentinel.
    const RenderInstrumentation bundle = ResolveInstrumentationBundle(
        RenderProfileMode::Gpu, &stats, &history, nullptr, kCaptureSentinel);

    EXPECT_EQ(bundle.GpuTimestamps, nullptr);
}
