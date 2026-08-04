#include <gtest/gtest.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimeService.h>
#include <time/SimClock.h>
#include <time/TimingHistory.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

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
// Platform clock contract
// =============================================================================

TEST(TimeService, DeltaAndUnscaledDeltaAreRawPlatformAliases)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_FLOAT_EQ(ft.Dt, ft.UnscaledDt);
    EXPECT_FLOAT_EQ(ft.Elapsed, ft.UnscaledElapsed);
    EXPECT_FLOAT_EQ(ft.Timescale, 1.0f);
}

TEST(TimeService, SecondAdvanceHasPositiveDelta)
{
    TimeService ts;
    ts.Advance();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    FrameClock ft = ts.Advance();
    EXPECT_GT(ft.UnscaledDt, 0.0f);
    EXPECT_GT(ft.Dt, 0.0f);
}

TEST(TimeService, ElapsedAccumulatesAcrossFrames)
{
    TimeService ts;
    ts.Advance();
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
// Fixed simulation loop
// =============================================================================

TEST(FixedSimulationLoop, BeginsFixedTicksWithConstantDelta)
{
    FixedSimulationLoop loop;

    FixedSimTime tick = loop.BeginFixedTick();
    EXPECT_DOUBLE_EQ(tick.DeltaSeconds, 1.0 / 60.0);
    EXPECT_EQ(tick.TickIndex, 0u);

    loop.EndFixedTick();
    tick = loop.BeginFixedTick();
    EXPECT_DOUBLE_EQ(tick.DeltaSeconds, 1.0 / 60.0);
    EXPECT_EQ(tick.TickIndex, 1u);
}

TEST(FixedSimulationLoop, BuildsClampedPresentationAlpha)
{
    FixedSimulationLoop loop;
    PresentationTime low = loop.BuildPresentationTime(-1.0);
    PresentationTime high = loop.BuildPresentationTime(2.0);

    EXPECT_DOUBLE_EQ(low.DeltaSeconds, loop.GetFixedDt());
    EXPECT_DOUBLE_EQ(low.Alpha, 0.0);
    EXPECT_DOUBLE_EQ(high.Alpha, 1.0);
}

// =============================================================================
// Timing history
// =============================================================================

TEST(TimingHistory, MaintainsBoundedChronologicalSamples)
{
    TimingHistory history(2);
    TimingFrameSample a{};
    a.FixedTicks = 1;
    TimingFrameSample b{};
    b.FixedTicks = 2;
    TimingFrameSample c{};
    c.FixedTicks = 3;

    history.Push(a);
    history.Push(b);
    history.Push(c);

    ASSERT_EQ(history.Size(), 2u);
    EXPECT_EQ(history.GetChronological(0).FixedTicks, 2u);
    EXPECT_EQ(history.GetChronological(1).FixedTicks, 3u);
    ASSERT_NE(history.Latest(), nullptr);
    EXPECT_EQ(history.Latest()->FixedTicks, 3u);
}

// =============================================================================
// Runtime frame loop
// =============================================================================

namespace
{
    // Scripts the platform clock so tick scheduling can be tested as a function
    // of elapsed time instead of by sleeping.
    class ScriptedClock
    {
    public:
        explicit ScriptedClock(RuntimeFrameLoop& runtime)
        {
            runtime.GetWallClock().SetNowSource([this] { return Now; });
        }

        void Advance(double seconds)
        {
            Now += std::chrono::duration_cast<TimeService::Clock::duration>(
                std::chrono::duration<double>(seconds));
        }

    private:
        TimeService::TimePoint Now{};
    };

    // One frame of the scheduling path: advance the clock, begin the frame,
    // resolve lifecycle, and take the tick budget.
    uint32_t StepFrame(RuntimeFrameLoop& runtime, ScriptedClock& clock, double seconds)
    {
        clock.Advance(seconds);
        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        const uint32_t ticks = runtime.ScheduleFixedTicks().TicksToRunThisFrame;
        for (uint32_t tick = 0; tick < ticks; ++tick)
        {
            (void)runtime.BeginFixedTick();
            runtime.EndFixedTick();
        }
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
        return ticks;
    }
}

TEST(RuntimeFrameLoop, DiscontinuityResetsResidualWithoutChangingTickTime)
{
    RuntimeFrameLoop runtime;
    ScriptedClock clock(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

    // Bank most of a tick, then cut simulated time.
    EXPECT_EQ(StepFrame(runtime, clock, 0.0), 0u); // first Advance() yields dt = 0
    EXPECT_EQ(StepFrame(runtime, clock, fixedDt * 0.9), 0u);

    clock.Advance(fixedDt * 0.9);
    runtime.BeginFrame();
    runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::SwapchainRecreated);
    runtime.ResolveLifecycleTransitions();
    const TickBudget budget = runtime.ScheduleFixedTicks();

    // Without the reset the two nine-tenths frames would have completed a tick.
    EXPECT_EQ(budget.TicksToRunThisFrame, 0u);
    EXPECT_DOUBLE_EQ(runtime.GetCurrentFrame().TickDtSeconds, fixedDt);
    EXPECT_TRUE(HasRuntimeFrameEvent(runtime.GetCurrentFrame().Events,
                                     RuntimeFrameEventFlags::TemporalDiscontinuity));
}

TEST(RuntimeFrameLoop, TickBudgetTracksScriptedWallClock)
{
    // The framerate-independence invariant at the layer that owns scheduling:
    // the same elapsed wall time yields the same simulated time, whether it
    // arrives as many short frames or few long ones. Simulated time may trail
    // by the tick still in progress, and by no more.
    const double frameDeltas[] = { 1.0 / 144.0, 1.0 / 60.0, 1.0 / 30.0 };
    for (double delta : frameDeltas)
    {
        RuntimeFrameLoop runtime;
        ScriptedClock clock(runtime);
        const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

        // The platform clock reports zero for its first sample by contract, so
        // prime it before measuring elapsed time against emitted ticks.
        StepFrame(runtime, clock, 0.0);

        uint64_t ticks = 0;
        double wallSeconds = 0.0;
        const int frames = static_cast<int>(1.0 / delta);
        for (int frame = 0; frame < frames; ++frame)
        {
            ticks += StepFrame(runtime, clock, delta);
            wallSeconds += delta;
        }

        // Wall deltas reach scheduling as float, so a delta that is an exact
        // multiple of the tick can land a hair under it and defer that tick to
        // the next frame. Lag therefore reaches one whole tick, and the
        // epsilon covers comparing that against this test's own double sum.
        constexpr double kRoundingEpsilon = 1e-9;
        const double simulated = static_cast<double>(ticks) * fixedDt;
        const double lag = wallSeconds - simulated;
        EXPECT_GE(lag, -kRoundingEpsilon) << "frame delta " << delta;
        EXPECT_LE(lag, fixedDt + kRoundingEpsilon) << "frame delta " << delta;
    }
}

TEST(RuntimeFrameLoop, SpikeFrameClampsTickBurst)
{
    RuntimeFrameLoop runtime;
    ScriptedClock clock(runtime);
    runtime.SetMaxFixedTicksPerFrame(4);

    EXPECT_EQ(StepFrame(runtime, clock, 0.0), 0u);
    const uint32_t ticks = StepFrame(runtime, clock, 1.0);

    EXPECT_EQ(ticks, 4u);
    EXPECT_GT(runtime.GetCurrentFrame().TicksDropped, 0u);
}

TEST(RuntimeFrameLoop, TimescaleScalesTickAccrualNotTickDelta)
{
    // Half timescale over the same wall time is half the simulated time, and
    // every tick is still the same length.
    const auto ticksAtTimescale = [](float timescale) {
        RuntimeFrameLoop runtime;
        ScriptedClock clock(runtime);
        runtime.SetSimulationTimescale(timescale);
        StepFrame(runtime, clock, 0.0);

        uint64_t ticks = 0;
        for (int frame = 0; frame < 120; ++frame)
        {
            ticks += StepFrame(runtime, clock, 1.0 / 120.0);
            EXPECT_DOUBLE_EQ(runtime.GetCurrentFrame().TickDtSeconds,
                             runtime.GetSimulationClock().GetFixedDt());
        }
        return ticks;
    };

    const uint64_t fullSpeed = ticksAtTimescale(1.0f);
    const uint64_t halfSpeed = ticksAtTimescale(0.5f);

    EXPECT_NEAR(static_cast<double>(fullSpeed), 60.0, 1.0);
    EXPECT_NEAR(static_cast<double>(halfSpeed), 30.0, 1.0);
}

TEST(RuntimeFrameLoop, PausedTimescaleAccruesNothing)
{
    RuntimeFrameLoop runtime;
    ScriptedClock clock(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();
    runtime.SetSimulationTimescale(0.0f);

    EXPECT_EQ(StepFrame(runtime, clock, 0.0), 0u);
    EXPECT_EQ(StepFrame(runtime, clock, 1.0), 0u);

    // Unpausing does not replay the wall time that passed while paused.
    runtime.SetSimulationTimescale(1.0f);
    EXPECT_EQ(StepFrame(runtime, clock, fixedDt * 1.5), 1u);
}

TEST(RuntimeFrameLoop, PresentationAlphaReportsResidual)
{
    RuntimeFrameLoop runtime;
    ScriptedClock clock(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

    EXPECT_EQ(StepFrame(runtime, clock, 0.0), 0u);
    EXPECT_EQ(StepFrame(runtime, clock, fixedDt * 0.25), 0u);

    EXPECT_NEAR(runtime.GetCurrentFrame().Presentation.Alpha, 0.25, 1e-5);
}

TEST(RuntimeFrameLoop, IdenticalScriptedClocksProduceIdenticalTickStreams)
{
    // Tick scheduling is owner-thread work with no parallel path, so the serial
    // reference is the whole contract: identical elapsed time in, identical
    // tick stream out.
    const auto run = [](std::vector<uint32_t>& perFrameTicks, uint64_t& finalTickIndex) {
        RuntimeFrameLoop runtime;
        ScriptedClock clock(runtime);
        const double deltas[] = { 0.004, 0.021, 0.007, 0.033, 0.012, 0.0166, 0.05 };
        for (int repeat = 0; repeat < 10; ++repeat)
            for (double delta : deltas)
                perFrameTicks.push_back(StepFrame(runtime, clock, delta));
        finalTickIndex = runtime.GetSimulationClock().GetTickIndex();
    };

    std::vector<uint32_t> first;
    std::vector<uint32_t> second;
    uint64_t firstIndex = 0;
    uint64_t secondIndex = 0;
    run(first, firstIndex);
    run(second, secondIndex);

    EXPECT_EQ(first, second);
    EXPECT_EQ(firstIndex, secondIndex);
    EXPECT_GT(firstIndex, 0u);
}

TEST(RuntimeFrameLoop, ResizeSettlesBeforeSwapchainRebuild)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.0);
    runtime.BeginFrame();
    runtime.NotifyResize(WindowExtent{ 1280, 720 });
    runtime.ResolveLifecycleTransitions();
    EXPECT_FALSE(runtime.ShouldRebuildSwapchain());

    runtime.EndFrame();
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    EXPECT_TRUE(runtime.ShouldRebuildSwapchain());
}

TEST(RuntimeFrameLoop, LifecycleOnlyFrameEmitsZeroTicks)
{
    RuntimeFrameLoop runtime;
    runtime.BeginFrame();
    runtime.NotifyMinimized();
    runtime.ResolveLifecycleTransitions();
    TickBudget budget = runtime.ScheduleFixedTicks();

    const RuntimeFrameSnapshot& frame = runtime.GetCurrentFrame();
    EXPECT_TRUE(frame.LifecycleOnly);
    EXPECT_EQ(budget.TicksToRunThisFrame, 0u);
}

TEST(RuntimeFrameLoop, TimescaleDoesNotScaleFixedTickDelta)
{
    RuntimeFrameLoop runtime;
    runtime.SetSimulationTimescale(10.0f);

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.ScheduleFixedTicks();
    FixedSimTime tick = runtime.BeginFixedTick();

    EXPECT_DOUBLE_EQ(tick.DeltaSeconds, runtime.GetSimulationClock().GetFixedDt());
}
