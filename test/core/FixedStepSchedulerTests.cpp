#include <time/FixedStepScheduler.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace
{
    constexpr double kFixedDt = 1.0 / 60.0;

    // Total ticks emitted over a scripted sequence of frame deltas, which is
    // what "simulation speed" means once frames stop being ticks.
    uint64_t RunFrames(FixedStepScheduler& scheduler,
                       const std::vector<double>& frameDeltas,
                       double fixedDt = kFixedDt)
    {
        uint64_t ticks = 0;
        for (double delta : frameDeltas)
            ticks += scheduler.Advance(delta, fixedDt).TicksToRun;
        return ticks;
    }
}

TEST(FixedStepScheduler, EmitsWholeTicksAsResidualCrossesFixedDt)
{
    FixedStepScheduler scheduler;

    // Half-tick frames: every second one completes a tick. Halving and
    // re-adding is exact in binary floating point, so the cadence is too.
    const double halfTick = kFixedDt * 0.5;
    for (int pair = 0; pair < 4; ++pair)
    {
        EXPECT_EQ(scheduler.Advance(halfTick, kFixedDt).TicksToRun, 0u) << "pair " << pair;
        EXPECT_EQ(scheduler.Advance(halfTick, kFixedDt).TicksToRun, 1u) << "pair " << pair;
    }
}

TEST(FixedStepScheduler, LowRefreshFrameRunsSeveralTicks)
{
    FixedStepScheduler scheduler;

    // A 30 Hz frame owes two 60 Hz ticks. This is the half-speed case.
    const FixedStepPlan plan = scheduler.Advance(1.0 / 30.0, kFixedDt);
    EXPECT_EQ(plan.TicksToRun, 2u);
    EXPECT_EQ(plan.TicksDropped, 0u);
}

TEST(FixedStepScheduler, TickTotalsTrackWallTimeAtAnyRefreshRate)
{
    // The invariant the bug violated: a second of frames is a second of
    // simulation, whatever the frame cadence is. Simulated time may trail wall
    // time by up to the tick that has not completed yet, and by no more.
    const double refreshRates[] = { 144.0, 120.0, 60.0, 59.94, 30.0 };
    for (double refresh : refreshRates)
    {
        FixedStepScheduler scheduler;
        const auto frameCount = static_cast<std::size_t>(refresh);
        const std::vector<double> frames(frameCount, 1.0 / refresh);

        const uint64_t ticks = RunFrames(scheduler, frames);

        double wallSeconds = 0.0;
        for (double frame : frames)
            wallSeconds += frame;

        const double simulatedSeconds = static_cast<double>(ticks) * kFixedDt;
        const double lag = wallSeconds - simulatedSeconds;

        // Summing the frames and multiplying out the ticks round differently,
        // so an exactly-caught-up sequence can land either side of zero.
        constexpr double kSummationEpsilon = 1e-12;
        EXPECT_GE(lag, -kSummationEpsilon) << "refresh " << refresh;
        EXPECT_LT(lag, kFixedDt) << "refresh " << refresh;
        EXPECT_NEAR(simulatedSeconds + scheduler.GetResidualSeconds(), wallSeconds, 1e-9)
            << "refresh " << refresh;
    }
}

TEST(FixedStepScheduler, ConservesInputAcrossIrregularFrames)
{
    FixedStepScheduler scheduler;
    std::mt19937 rng(1337u);
    std::uniform_real_distribution<double> jitter(0.001, 0.05);

    double totalInput = 0.0;
    uint64_t ticks = 0;
    for (int frame = 0; frame < 500; ++frame)
    {
        const double delta = jitter(rng);
        totalInput += delta;
        const FixedStepPlan plan = scheduler.Advance(delta, kFixedDt);
        ticks += plan.TicksToRun;
        ASSERT_EQ(plan.TicksDropped, 0u) << "frame " << frame;
        ASSERT_LT(scheduler.GetResidualSeconds(), kFixedDt);
    }

    // Emitted time plus what is still owed equals what went in.
    const double emitted = static_cast<double>(ticks) * kFixedDt;
    EXPECT_NEAR(emitted + scheduler.GetResidualSeconds(), totalInput, 1e-9);
}

TEST(FixedStepScheduler, CapDropsBacklogAndKeepsResidualBelowOneTick)
{
    FixedStepScheduler scheduler;
    scheduler.SetMaxTicksPerFrame(4);
    scheduler.SetMaxFrameInputSeconds(2.0); // isolate the tick cap from the input clamp

    const FixedStepPlan plan = scheduler.Advance(1.0, kFixedDt);

    EXPECT_EQ(plan.TicksToRun, 4u);
    EXPECT_EQ(plan.TicksDropped, 56u);
    EXPECT_LT(scheduler.GetResidualSeconds(), kFixedDt);

    // The dropped backlog does not come back as a second overrun next frame.
    const FixedStepPlan next = scheduler.Advance(kFixedDt, kFixedDt);
    EXPECT_LE(next.TicksToRun, 2u);
    EXPECT_EQ(next.TicksDropped, 0u);
}

TEST(FixedStepScheduler, InputClampBoundsASingleFrame)
{
    FixedStepScheduler scheduler;
    scheduler.SetMaxFrameInputSeconds(0.25);
    scheduler.SetMaxTicksPerFrame(1000);

    const FixedStepPlan plan = scheduler.Advance(30.0, kFixedDt);

    EXPECT_NEAR(plan.ClampedInputSeconds, 29.75, 1e-9);
    EXPECT_EQ(plan.TicksToRun, 15u); // 0.25 s of 1/60 s ticks
}

TEST(FixedStepScheduler, ResetDropsResidual)
{
    FixedStepScheduler scheduler;
    (void)scheduler.Advance(kFixedDt * 0.9, kFixedDt);
    ASSERT_GT(scheduler.GetResidualSeconds(), 0.0);

    scheduler.Reset();

    EXPECT_EQ(scheduler.GetResidualSeconds(), 0.0);
    // Without the reset this frame would have completed the pending tick.
    EXPECT_EQ(scheduler.Advance(kFixedDt * 0.9, kFixedDt).TicksToRun, 0u);
}

TEST(FixedStepScheduler, AlphaReportsResidualAsFractionOfTick)
{
    FixedStepScheduler scheduler;

    (void)scheduler.Advance(kFixedDt * 0.25, kFixedDt);
    EXPECT_NEAR(scheduler.GetAlpha(kFixedDt), 0.25, 1e-6);

    const FixedStepPlan plan = scheduler.Advance(kFixedDt * 0.5, kFixedDt);
    EXPECT_NEAR(plan.Alpha, 0.75, 1e-6);

    // Completing a tick leaves the leftover behind it, not a full tick.
    const FixedStepPlan crossing = scheduler.Advance(kFixedDt * 0.5, kFixedDt);
    EXPECT_EQ(crossing.TicksToRun, 1u);
    EXPECT_NEAR(crossing.Alpha, 0.25, 1e-6);
}

TEST(FixedStepScheduler, IgnoresNonPositiveAndNonFiniteInput)
{
    FixedStepScheduler scheduler;

    EXPECT_EQ(scheduler.Advance(0.0, kFixedDt).TicksToRun, 0u);
    EXPECT_EQ(scheduler.Advance(-1.0, kFixedDt).TicksToRun, 0u);
    EXPECT_EQ(scheduler.Advance(
        std::numeric_limits<double>::quiet_NaN(), kFixedDt).TicksToRun, 0u);
    EXPECT_EQ(scheduler.GetResidualSeconds(), 0.0);

    // A zero or nonsense tick length cannot divide time; emit nothing.
    EXPECT_EQ(scheduler.Advance(1.0, 0.0).TicksToRun, 0u);
}

TEST(FixedStepScheduler, HonoursConfiguredTickRate)
{
    FixedStepScheduler scheduler;
    const double fixedDt = 1.0 / 120.0;

    // One 60 Hz frame owes two 120 Hz ticks.
    EXPECT_EQ(scheduler.Advance(1.0 / 60.0, fixedDt).TicksToRun, 2u);
}
