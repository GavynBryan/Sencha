#include <gtest/gtest.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimeService.h>
#include <time/SimClock.h>
#include <time/TimingHistory.h>

#include <chrono>
#include <thread>

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

TEST(RuntimeFrameLoop, DiscontinuityProducesHistoryResetWithoutChangingTickTime)
{
    RuntimeFrameLoop runtime;
    runtime.BeginFrame();
    runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::SwapchainRecreated);
    TickBudget budget = runtime.ScheduleFixedTicks();

    EXPECT_EQ(budget.TicksToRunThisFrame, 1u);
    EXPECT_DOUBLE_EQ(runtime.GetCurrentFrame().TickDtSeconds, runtime.GetSimulationClock().GetFixedDt());
    EXPECT_TRUE(runtime.ConsumePresentationHistoryReset());
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
