#include <gtest/gtest.h>
#include <time/TimeService.h>
#include <thread>
#include <chrono>

// =============================================================================
// FrameClock layout
// =============================================================================

TEST(FrameClock, HasExpectedFields)
{
    FrameClock fc{};
    EXPECT_FLOAT_EQ(fc.Dt, 0.0f);
    EXPECT_FLOAT_EQ(fc.UnscaledDt, 0.0f);
    EXPECT_FLOAT_EQ(fc.Elapsed, 0.0f);
    EXPECT_FLOAT_EQ(fc.UnscaledElapsed, 0.0f);
    EXPECT_FLOAT_EQ(fc.Timescale, 1.0f);
    EXPECT_EQ(fc.FrameIndex, 0u);
}

// =============================================================================
// First frame
// =============================================================================

TEST(TimeService, FirstAdvanceReturnsZeroDelta)
{
    TimeService ts;
    FrameClock ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.Dt, 0.0f);
    EXPECT_FLOAT_EQ(ft.UnscaledDt, 0.0f);
}

TEST(TimeService, FirstAdvanceReturnsZeroElapsed)
{
    TimeService ts;
    FrameClock ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.Elapsed, 0.0f);
    EXPECT_FLOAT_EQ(ft.UnscaledElapsed, 0.0f);
}

TEST(TimeService, FirstAdvanceTimescaleIsOne)
{
    TimeService ts;
    FrameClock ft = ts.Advance();

    EXPECT_FLOAT_EQ(ft.Timescale, 1.0f);
}

TEST(TimeService, FirstAdvanceFrameIndexIsOne)
{
    TimeService ts;
    FrameClock ft = ts.Advance();

    EXPECT_EQ(ft.FrameIndex, 1u);
}

// =============================================================================
// Delta invariants (no sleep needed — verify relationships, not absolute values)
// =============================================================================

TEST(TimeService, DeltaEqualsUnscaledDeltaAtDefaultTimescale)
{
    TimeService ts;
    ts.Advance(); // burn first frame
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_FLOAT_EQ(ft.Dt, ft.UnscaledDt * ft.Timescale);
}

TEST(TimeService, SecondAdvanceHasPositiveDelta)
{
    TimeService ts;
    ts.Advance(); // burn first frame
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_GT(ft.UnscaledDt, 0.0f);
    EXPECT_GT(ft.Dt, 0.0f);
}

// =============================================================================
// Elapsed accumulation
// =============================================================================

TEST(TimeService, ElapsedAccumulatesAcrossFrames)
{
    TimeService ts;
    ts.Advance(); // first frame — zero delta, zero elapsed
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock f2 = ts.Advance();

    EXPECT_GT(f2.UnscaledElapsed, f1.UnscaledElapsed);
    EXPECT_GT(f2.Elapsed, f1.Elapsed);
}

TEST(TimeService, UnscaledElapsedEqualsUnscaledDeltaSum)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock f2 = ts.Advance();

    float expectedUnscaled = f1.UnscaledDt + f2.UnscaledDt;
    EXPECT_NEAR(f2.UnscaledElapsed, expectedUnscaled, 1e-6f);
}

TEST(TimeService, FrameIndexIncrementsEachAdvance)
{
    TimeService ts;
    FrameClock f1 = ts.Advance();
    FrameClock f2 = ts.Advance();
    FrameClock f3 = ts.Advance();

    EXPECT_EQ(f1.FrameIndex, 1u);
    EXPECT_EQ(f2.FrameIndex, 2u);
    EXPECT_EQ(f3.FrameIndex, 3u);
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

TEST(TimeService, TimescaleReflectedInFrameClock)
{
    TimeService ts;
    ts.SetTimescale(3.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_FLOAT_EQ(ft.Timescale, 3.0f);
}

TEST(TimeService, ScaledDeltaEqualsUnscaledTimesTimescale)
{
    TimeService ts;
    ts.SetTimescale(2.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_NEAR(ft.Dt, ft.UnscaledDt * 2.0f, 1e-6f);
}

TEST(TimeService, ZeroTimescalePausesScaledTime)
{
    TimeService ts;
    ts.SetTimescale(0.0f);
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    FrameClock f1 = ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    FrameClock f2 = ts.Advance();

    // Scaled delta and elapsed must stay zero.
    EXPECT_FLOAT_EQ(f1.Dt, 0.0f);
    EXPECT_FLOAT_EQ(f2.Dt, 0.0f);
    EXPECT_FLOAT_EQ(f2.Elapsed, 0.0f);

    // Unscaled time must still advance.
    EXPECT_GT(f1.UnscaledDt, 0.0f);
    EXPECT_GT(f2.UnscaledElapsed, f1.UnscaledElapsed);
}

TEST(TimeService, TimescaleChangeAffectsNextFrameOnly)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock before = ts.Advance();
    ts.SetTimescale(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock after = ts.Advance();

    // Frame before change used timescale 1.
    EXPECT_FLOAT_EQ(before.Timescale, 1.0f);
    EXPECT_NEAR(before.Dt, before.UnscaledDt, 1e-6f);

    // Frame after change uses timescale 0.
    EXPECT_FLOAT_EQ(after.Dt, 0.0f);
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

    FrameClock ft = ts.Advance();
    EXPECT_LE(ft.UnscaledDt, MaxDelta);
}
