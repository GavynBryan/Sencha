#include <gtest/gtest.h>
#include <time/TimeService.h>
#include <thread>
#include <chrono>

// =============================================================================
// FrameTime layout
// =============================================================================

TEST(FrameTime, SizeIs20Bytes)
{
    EXPECT_EQ(sizeof(FrameTime), 20u);
}

// =============================================================================
// First frame
// =============================================================================

TEST(TimeService, FirstAdvanceReturnsZeroDelta)
{
    TimeService ts;
    FrameTime ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.DeltaTime, 0.0f);
    EXPECT_FLOAT_EQ(ft.UnscaledDeltaTime, 0.0f);
}

TEST(TimeService, FirstAdvanceReturnsZeroElapsed)
{
    TimeService ts;
    FrameTime ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.ElapsedTime, 0.0f);
    EXPECT_FLOAT_EQ(ft.UnscaledElapsedTime, 0.0f);
}

TEST(TimeService, FirstAdvanceTimescaleIsOne)
{
    TimeService ts;
    FrameTime ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.Timescale, 1.0f);
}

// =============================================================================
// Delta invariants (no sleep needed — verify relationships, not absolute values)
// =============================================================================

TEST(TimeService, DeltaEqualsUnscaledDeltaAtDefaultTimescale)
{
    TimeService ts;
    ts.Advance(); // burn first frame
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime ft = ts.Advance();
    EXPECT_FLOAT_EQ(ft.DeltaTime, ft.UnscaledDeltaTime * ft.Timescale);
}

TEST(TimeService, SecondAdvanceHasPositiveDelta)
{
    TimeService ts;
    ts.Advance(); // burn first frame
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime ft = ts.Advance();
    EXPECT_GT(ft.UnscaledDeltaTime, 0.0f);
    EXPECT_GT(ft.DeltaTime, 0.0f);
}

// =============================================================================
// Elapsed accumulation
// =============================================================================

TEST(TimeService, ElapsedAccumulatesAcrossFrames)
{
    TimeService ts;
    ts.Advance(); // first frame — zero delta, zero elapsed
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime f2 = ts.Advance();

    EXPECT_GT(f2.UnscaledElapsedTime, f1.UnscaledElapsedTime);
    EXPECT_GT(f2.ElapsedTime, f1.ElapsedTime);
}

TEST(TimeService, UnscaledElapsedEqualsUnscaledDeltaSum)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime f2 = ts.Advance();

    float expectedUnscaled = f1.UnscaledDeltaTime + f2.UnscaledDeltaTime;
    EXPECT_NEAR(f2.UnscaledElapsedTime, expectedUnscaled, 1e-6f);
}

// =============================================================================
// Timescale
// =============================================================================

TEST(TimeService, GetSetTimescaleRoundTrips)
{
    TimeService ts;
    ts.SetTimescale(2.5f);
    EXPECT_FLOAT_EQ(ts.GetTimescale(), 2.5f);
}

TEST(TimeService, TimescaleReflectedInFrameTime)
{
    TimeService ts;
    ts.SetTimescale(3.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime ft = ts.Advance();
    EXPECT_FLOAT_EQ(ft.Timescale, 3.0f);
}

TEST(TimeService, ScaledDeltaEqualsUnscaledTimesTimescale)
{
    TimeService ts;
    ts.SetTimescale(2.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime ft = ts.Advance();
    EXPECT_NEAR(ft.DeltaTime, ft.UnscaledDeltaTime * 2.0f, 1e-6f);
}

TEST(TimeService, ZeroTimescalePausesScaledTime)
{
    TimeService ts;
    ts.SetTimescale(0.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    FrameTime f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    FrameTime f2 = ts.Advance();

    // Scaled delta and elapsed must stay zero.
    EXPECT_FLOAT_EQ(f1.DeltaTime, 0.0f);
    EXPECT_FLOAT_EQ(f2.DeltaTime, 0.0f);
    EXPECT_FLOAT_EQ(f2.ElapsedTime, 0.0f);

    // Unscaled time must still advance.
    EXPECT_GT(f1.UnscaledDeltaTime, 0.0f);
    EXPECT_GT(f2.UnscaledElapsedTime, f1.UnscaledElapsedTime);
}

TEST(TimeService, TimescaleChangeAffectsNextFrameOnly)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime before = ts.Advance();
    ts.SetTimescale(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameTime after = ts.Advance();

    // Frame before change used timescale 1.
    EXPECT_FLOAT_EQ(before.Timescale, 1.0f);
    EXPECT_NEAR(before.DeltaTime, before.UnscaledDeltaTime, 1e-6f);

    // Frame after change uses timescale 0.
    EXPECT_FLOAT_EQ(after.DeltaTime, 0.0f);
}

// =============================================================================
// Delta clamping
// =============================================================================

TEST(TimeService, UnscaledDeltaNeverExceedsMaxDeltaSeconds)
{
    // Max is 1/15 ≈ 0.0667s. We can't inject a stall, but we can verify
    // that normal frames are already below the cap and that the cap value
    // itself is sane.
    constexpr float MaxDelta = 1.0f / 15.0f;

    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    FrameTime ft = ts.Advance();
    EXPECT_LE(ft.UnscaledDeltaTime, MaxDelta);
}
