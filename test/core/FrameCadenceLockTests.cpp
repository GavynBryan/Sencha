#include <gtest/gtest.h>

#include <time/FrameCadenceLock.h>

#include <cmath>
#include <vector>

//=============================================================================
// The frame delta the simulation consumes tracks the cadence frames present
// at, not the noise the CPU measured it with.
//
// The defect this guards: CPU-side deltas on a vsynced display jitter by
// milliseconds while frames present on an exact period. Consuming the
// measurement raw makes the tick accumulator run bursts it does not owe and
// samples interpolation at times the display never shows -- spatial jitter
// proportional to speed, at any refresh rate.
//=============================================================================

namespace
{
    constexpr double kPeriod60 = 1.0 / 60.0;

    // Deterministic noise in [-amplitude, amplitude]: a fixed pattern, not a
    // seeded generator, so a failure replays exactly.
    double NoiseAt(std::size_t index, double amplitude)
    {
        static constexpr double pattern[] = { 0.31, -0.74, 0.55, -0.12, 0.93,
                                              -0.48, 0.07, -0.91, 0.66, -0.28 };
        return pattern[index % std::size(pattern)] * amplitude;
    }

    // Runs enough clean frames for the lock to gather a window and engage.
    void Prime(FrameCadenceLock& lock, double period, std::size_t frames = 64)
    {
        for (std::size_t i = 0; i < frames; ++i)
            (void)lock.Apply(period + NoiseAt(i, period * 0.05));
    }
}

TEST(FrameCadenceLock, SnapsNoisyDeltasToTheDiscoveredPeriod)
{
    FrameCadenceLock lock;
    Prime(lock, kPeriod60, 128);
    ASSERT_TRUE(lock.IsLocked());

    // Two milliseconds of measurement noise on a 60 Hz cadence: the exact
    // shape observed live. The output must hold the period -- close to it in
    // absolute terms, and above all steady frame to frame, because the
    // frame-to-frame churn is what interpolation turns into visible jitter.
    double previous = lock.Apply(kPeriod60 + NoiseAt(0, 0.002));
    for (std::size_t i = 1; i < 120; ++i)
    {
        const double measured = kPeriod60 + NoiseAt(i, 0.002);
        const double out = lock.Apply(measured);
        EXPECT_NEAR(out, kPeriod60, kPeriod60 * 0.01)
            << "frame " << i << " measured " << measured;
        EXPECT_LT(std::abs(out - previous), 30e-6)
            << "output churn at frame " << i
            << " (raw noise here is two thousand microseconds)";
        previous = out;
    }
}

TEST(FrameCadenceLock, ConservesElapsedTimeWhileSnapping)
{
    FrameCadenceLock lock;
    Prime(lock, kPeriod60);

    double measuredTotal = 0.0;
    double outputTotal = 0.0;
    for (std::size_t i = 0; i < 2000; ++i)
    {
        const double measured = kPeriod60 + NoiseAt(i, 0.0015);
        measuredTotal += measured;
        outputTotal += lock.Apply(measured);
    }

    // The carried error is repaid a few microseconds per frame, so totals track
    // within a fraction of one period even over thousands of frames.
    EXPECT_NEAR(outputTotal, measuredTotal, kPeriod60 * 0.5);
}

TEST(FrameCadenceLock, ADroppedFrameSnapsToTwoPeriods)
{
    FrameCadenceLock lock;
    Prime(lock, kPeriod60);

    const double out = lock.Apply(2.0 * kPeriod60 + 0.0018);
    EXPECT_NEAR(out, 2.0 * kPeriod60, kPeriod60 * 0.01)
        << "a missed vblank is two clean periods, not one noisy measurement";
}

TEST(FrameCadenceLock, AHitchPassesThroughUntouched)
{
    FrameCadenceLock lock;
    Prime(lock, kPeriod60);

    // A quarter-second stall is real elapsed time the simulation owes; the
    // ceiling on snappable periods refuses to tidy it.
    const double hitch = 0.25;
    EXPECT_DOUBLE_EQ(lock.Apply(hitch), hitch);
}

TEST(FrameCadenceLock, AnAperiodicStreamNeverEngages)
{
    FrameCadenceLock lock;

    // Uncapped frame times sweeping 4-12 ms with no recurring period.
    for (std::size_t i = 0; i < 200; ++i)
    {
        const double measured = 0.004 + 0.001 * static_cast<double>(i % 9);
        const double out = lock.Apply(measured);
        EXPECT_DOUBLE_EQ(out, measured) << "frame " << i;
    }
    EXPECT_FALSE(lock.IsLocked());
}

TEST(FrameCadenceLock, RelocksAfterACadenceChange)
{
    FrameCadenceLock lock;
    Prime(lock, kPeriod60);
    ASSERT_TRUE(lock.IsLocked());

    // The display moves to 144 Hz. Old-period samples age out of the window
    // and the lock re-engages on the new cadence.
    constexpr double period144 = 1.0 / 144.0;
    for (std::size_t i = 0; i < 128; ++i)
        (void)lock.Apply(period144 + NoiseAt(i, period144 * 0.05));

    ASSERT_TRUE(lock.IsLocked());
    EXPECT_NEAR(lock.PeriodSeconds(), period144, period144 * 0.06);

    const double out = lock.Apply(period144 + 0.0009);
    EXPECT_NEAR(out, period144, period144 * 0.02);
}

TEST(FrameCadenceLock, DisabledPassesEverythingThrough)
{
    FrameCadenceLock lock;
    lock.SetEnabled(false);
    Prime(lock, kPeriod60);

    const double measured = kPeriod60 + 0.002;
    EXPECT_DOUBLE_EQ(lock.Apply(measured), measured);
    EXPECT_FALSE(lock.IsLocked());
}

TEST(FrameCadenceLock, ExactDeltasPassUnchangedWhileLocked)
{
    // Scripted-clock tests and the frame pacer feed exact periods; the lock
    // must be an identity for them or deterministic tests would drift.
    FrameCadenceLock lock;
    for (std::size_t i = 0; i < 64; ++i)
        (void)lock.Apply(kPeriod60);

    ASSERT_TRUE(lock.IsLocked());
    EXPECT_DOUBLE_EQ(lock.Apply(kPeriod60), kPeriod60);
}
