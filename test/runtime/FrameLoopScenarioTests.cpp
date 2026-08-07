#include <gtest/gtest.h>
#include <input/InputFrame.h>
#include <runtime/FrameDiscontinuityBus.h>
#include <runtime/FrameDriver.h>
#include <runtime/FramePacer.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <runtime/SwapchainRebuildWorker.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

// Keep the runtime test suite self-contained: we reference scancode values by
// number so the suite does not depend on SDL headers. 26 is SDL_SCANCODE_W.
static constexpr uint32_t kTestScancode = 26;

namespace
{
    // Scripts the platform clock so a scenario controls how much simulated time
    // each frame is worth. Tick emission is a function of elapsed wall time, so
    // scheduling behavior is only testable deterministically by scripting it.
    class ScriptedFrameClock
    {
    public:
        explicit ScriptedFrameClock(RuntimeFrameLoop& runtime)
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
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Discontinuity bus
// ---------------------------------------------------------------------------

TEST(FrameDiscontinuityBus, SubscribedListenerFiresOnPublish)
{
    FrameDiscontinuityBus bus;
    int calls = 0;
    TemporalDiscontinuityReason seen = TemporalDiscontinuityReason::None;
    bus.Subscribe([&](const FrameDiscontinuityEvent& e) {
        ++calls;
        seen = e.Reason;
    });
    bus.Publish({ TemporalDiscontinuityReason::Teleport, 42 });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen, TemporalDiscontinuityReason::Teleport);
}

TEST(FrameDiscontinuityBus, UnsubscribeStopsDelivery)
{
    FrameDiscontinuityBus bus;
    int calls = 0;
    auto token = bus.Subscribe([&](const FrameDiscontinuityEvent&) { ++calls; });
    bus.Publish({ TemporalDiscontinuityReason::Resize, 1 });
    bus.Unsubscribe(token);
    bus.Publish({ TemporalDiscontinuityReason::Resize, 2 });
    EXPECT_EQ(calls, 1);
}

TEST(RuntimeFrameLoop, SwapchainRecreatePublishesOnBus)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.0);
    int busCalls = 0;
    TemporalDiscontinuityReason busReason = TemporalDiscontinuityReason::None;
    runtime.GetDiscontinuityBus().Subscribe([&](const FrameDiscontinuityEvent& e) {
        ++busCalls;
        busReason = e.Reason;
    });
    runtime.BeginFrame();
    runtime.NotifyResize(WindowExtent{ 800, 600 });
    runtime.ResolveLifecycleTransitions();
    // Second frame — settle counter reaches 0, rebuild ready.
    runtime.EndFrame();
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.BeginSwapchainRebuild();
    runtime.CompleteSwapchainRebuild(WindowExtent{ 800, 600 });
    EXPECT_EQ(busCalls, 1);
    EXPECT_EQ(busReason, TemporalDiscontinuityReason::SwapchainRecreated);
}

// ---------------------------------------------------------------------------
// Scenario: burst resize
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, BurstResizeEmitsZeroTicksDuringLifecycleFrames)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.0);
    for (int i = 0; i < 60; ++i)
    {
        runtime.BeginFrame();
        if (i % 3 == 0)
            runtime.NotifyResize(WindowExtent{ 1280u + uint32_t(i), 720u });
        runtime.ResolveLifecycleTransitions();
        TickBudget budget = runtime.ScheduleFixedTicks();
        if (runtime.GetCurrentFrame().LifecycleOnly)
        {
            EXPECT_EQ(budget.TicksToRunThisFrame, 0u);
        }
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
    }
}

TEST(RuntimeFrameLoopScenario, ResizeQuietWindowPreventsImmediateRebuild)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.02);

    runtime.BeginFrame();
    runtime.NotifyResize(WindowExtent{ 1280, 720 });
    runtime.ResolveLifecycleTransitions();
    EXPECT_FALSE(runtime.ShouldRebuildSwapchain());
    runtime.EndFrame();

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    EXPECT_FALSE(runtime.ShouldRebuildSwapchain());
    runtime.EndFrame();

    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    EXPECT_TRUE(runtime.ShouldRebuildSwapchain());
    runtime.EndFrame();
}

// ---------------------------------------------------------------------------
// Scenario: minimized-for-long-time does not emit simulation ticks
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, LongMinimizeEmitsZeroTicks)
{
    RuntimeFrameLoop runtime;
    runtime.BeginFrame();
    runtime.NotifyMinimized();
    runtime.ResolveLifecycleTransitions();
    EXPECT_EQ(runtime.ScheduleFixedTicks().TicksToRunThisFrame, 0u);
    runtime.EndFrame();

    // Sleep a realistic-ish stall (5ms — enough to exceed fixed dt of ~16.7ms? no,
    // use injected delta instead).
    for (int i = 0; i < 20; ++i)
    {
        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        EXPECT_EQ(runtime.ScheduleFixedTicks().TicksToRunThisFrame, 0u);
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
    }
}

// ---------------------------------------------------------------------------
// Scenario: repeated swapchain invalidation
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, RepeatedSwapchainInvalidationNeverTrapsInLifecycle)
{
    RuntimeFrameLoop runtime;
    runtime.SetSurfaceExtent(WindowExtent{ 1280, 720 });

    int completeCount = 0;
    for (int i = 0; i < 10; ++i)
    {
        runtime.BeginFrame();
        runtime.NotifySwapchainInvalidated();
        runtime.ResolveLifecycleTransitions();
        runtime.EndFrame();

        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        if (runtime.ShouldRebuildSwapchain())
        {
            runtime.BeginSwapchainRebuild();
            runtime.CompleteSwapchainRebuild(WindowExtent{ 1280, 720 });
            ++completeCount;
        }
        runtime.EndFrame();
    }
    EXPECT_EQ(completeCount, 10);
}

TEST(RuntimeFrameLoopScenario, StaleSwapchainRebuildKeepsLatestResizeDirty)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.0);

    runtime.BeginFrame();
    runtime.NotifyResize(WindowExtent{ 1280, 720 });
    runtime.ResolveLifecycleTransitions();
    runtime.EndFrame();

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    ASSERT_TRUE(runtime.ShouldRebuildSwapchain());
    runtime.BeginSwapchainRebuild();

    runtime.NotifyResize(WindowExtent{ 1600, 900 });
    runtime.CompleteSwapchainRebuild(WindowExtent{ 1280, 720 });

    EXPECT_EQ(runtime.GetDesiredSwapchainExtent().Width, 1600u);
    EXPECT_EQ(runtime.GetDesiredSwapchainExtent().Height, 900u);
    EXPECT_EQ(runtime.GetState(), RuntimeFrameState::SwapchainInvalid);
    EXPECT_TRUE(runtime.GetCurrentFrame().LifecycleOnly);
    runtime.EndFrame();

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.EndFrame();

    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    EXPECT_TRUE(runtime.ShouldRebuildSwapchain());
    EXPECT_EQ(runtime.GetDesiredSwapchainExtent().Width, 1600u);
    EXPECT_EQ(runtime.GetDesiredSwapchainExtent().Height, 900u);
    runtime.EndFrame();
}

// ---------------------------------------------------------------------------
// Scenario: timescale pause freezes simulation tick count
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, TimescaleZeroPausesSimulationTicks)
{
    RuntimeFrameLoop runtime;
    runtime.SetSimulationTimescale(0.0f);

    uint32_t fixedTicks = 0;
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.ScheduleFixedTicks();
    while (runtime.CanRunFixedTickThisFrame())
    {
        (void)runtime.BeginFixedTick();
        runtime.EndFixedTick();
        ++fixedTicks;
    }
    runtime.BuildPresentationFrame();
    runtime.EndFrame();

    EXPECT_EQ(fixedTicks, 0u);
}

TEST(RuntimeFrameLoopScenario, TimescaleRealtimeProducesTicks)
{
    RuntimeFrameLoop runtime;
    ScriptedFrameClock clock(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

    const auto stepFrame = [&](double seconds) {
        clock.Advance(seconds);
        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        runtime.ScheduleFixedTicks();
        uint32_t ticks = 0;
        while (runtime.CanRunFixedTickThisFrame())
        {
            (void)runtime.BeginFixedTick();
            runtime.EndFixedTick();
            ++ticks;
        }
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
        return ticks;
    };

    stepFrame(0.0);
    EXPECT_EQ(stepFrame(fixedDt * 1.5), 1u);
}

// ---------------------------------------------------------------------------
// Scenario: a discontinuity publishes once and is visible in the snapshot
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, DiscontinuityPublishesOnceAndFlagsTheFrame)
{
    RuntimeFrameLoop runtime;
    int busCalls = 0;
    TemporalDiscontinuityReason busReason = TemporalDiscontinuityReason::None;
    runtime.GetDiscontinuityBus().Subscribe(
        [&](const FrameDiscontinuityEvent& event)
        {
            ++busCalls;
            busReason = event.Reason;
        });

    runtime.BeginFrame();
    runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::Teleport);
    runtime.ScheduleFixedTicks();

    EXPECT_EQ(busCalls, 1);
    EXPECT_EQ(busReason, TemporalDiscontinuityReason::Teleport);
    EXPECT_TRUE(HasRuntimeFrameEvent(runtime.GetCurrentFrame().Events,
                                     RuntimeFrameEventFlags::TemporalDiscontinuity));

    // Applying is idempotent within the frame: the pending mark is cleared, so
    // a second schedule does not republish.
    runtime.ScheduleFixedTicks();
    EXPECT_EQ(busCalls, 1);
}

// ---------------------------------------------------------------------------
// InputFrame edges
// ---------------------------------------------------------------------------

TEST(InputFrame, EdgesDrainedAfterClear)
{
    InputFrame frame;
    frame.KeysPressed.push_back(42);
    frame.MouseButtonsPressed.push_back(1);
    frame.ClearEdges();
    EXPECT_TRUE(frame.KeysPressed.empty());
    EXPECT_TRUE(frame.MouseButtonsPressed.empty());
}

TEST(InputFrame, HeldStateSurvivesEdgeClear)
{
    InputFrame frame;
    frame.SetKeyHeld(kTestScancode, true);
    frame.KeysPressed.push_back(kTestScancode);
    frame.ClearEdges();
    EXPECT_TRUE(frame.IsKeyDown(kTestScancode));
}

TEST(InputFrame, ConsumeKeyPressedRemovesAllMatchingEdges)
{
    InputFrame frame;
    frame.KeysPressed.push_back(kTestScancode);
    frame.KeysPressed.push_back(99);
    frame.KeysPressed.push_back(kTestScancode);

    EXPECT_TRUE(frame.ConsumeKeyPressed(kTestScancode));
    EXPECT_EQ(frame.KeysPressed.size(), 1u);
    EXPECT_EQ(frame.KeysPressed.front(), 99u);

    // A second consume finds nothing, which is what stops a handler outside the
    // fixed-tick drain from acting on the same press twice.
    EXPECT_FALSE(frame.ConsumeKeyPressed(kTestScancode));
}

TEST(FrameDriver, FirstTickSeesInputEdges)
{
    RuntimeFrameLoop runtime;
    ScriptedFrameClock clock(runtime);
    FrameDriver driver(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

    int tickCount = 0;
    std::size_t firstTickPressed = 0;
    bool pressThisFrame = true;

    driver.Register(FramePhase::PumpPlatform, [&](PhaseContext& ctx) {
        if (!pressThisFrame)
            return;
        ctx.Input->SetKeyHeld(kTestScancode, true);
        ctx.Input->KeysPressed.push_back(kTestScancode);
    });
    driver.Register(FramePhase::ScheduleTicks, [&](PhaseContext& ctx) {
        ctx.Runtime->ScheduleFixedTicks();
    });
    driver.Register(FramePhase::Simulate, [&](PhaseContext& ctx) {
        if (tickCount == 0)
            firstTickPressed = ctx.Input->KeysPressed.size();
        ++tickCount;
    });

    // The platform clock reports zero for its first sample, so the opening
    // frame accrues no simulated time and runs no tick.
    clock.Advance(0.0);
    driver.StepOnce();
    pressThisFrame = false;

    clock.Advance(fixedDt * 1.5);
    driver.StepOnce();

    EXPECT_EQ(tickCount, 1);
    EXPECT_EQ(firstTickPressed, 1u);
    EXPECT_TRUE(driver.GetInputFrame().IsKeyDown(kTestScancode));
}

TEST(FrameDriver, EdgesPersistAcrossZeroTickFramesUntilFirstTick)
{
    RuntimeFrameLoop runtime;
    ScriptedFrameClock clock(runtime);
    FrameDriver driver(runtime);
    const double fixedDt = runtime.GetSimulationClock().GetFixedDt();

    int ticksSeen = 0;
    int ticksThatSawTheEdge = 0;
    bool pressThisFrame = false;

    driver.Register(FramePhase::PumpPlatform, [&](PhaseContext& ctx) {
        if (pressThisFrame)
            ctx.Input->KeysPressed.push_back(kTestScancode);
    });
    driver.Register(FramePhase::ScheduleTicks, [&](PhaseContext& ctx) {
        ctx.Runtime->ScheduleFixedTicks();
    });
    driver.Register(FramePhase::Simulate, [&](PhaseContext& ctx) {
        ++ticksSeen;
        for (uint32_t scancode : ctx.Input->KeysPressed)
            if (scancode == kTestScancode)
                ++ticksThatSawTheEdge;
    });

    // Press during a frame too short to complete a tick.
    clock.Advance(0.0);
    driver.StepOnce();
    pressThisFrame = true;
    clock.Advance(fixedDt * 0.4);
    driver.StepOnce();
    pressThisFrame = false;
    ASSERT_EQ(ticksSeen, 0);

    // The press waits for the tick rather than being lost with the frame.
    clock.Advance(fixedDt * 0.4);
    driver.StepOnce();
    clock.Advance(fixedDt * 0.4);
    driver.StepOnce();

    EXPECT_GE(ticksSeen, 1);
    EXPECT_EQ(ticksThatSawTheEdge, 1);
}

TEST(FrameDriver, SpikeFrameRunsCappedTickBurst)
{
    RuntimeFrameLoop runtime;
    ScriptedFrameClock clock(runtime);
    runtime.SetMaxFixedTicksPerFrame(4);
    FrameDriver driver(runtime);

    int ticksSeen = 0;
    driver.Register(FramePhase::ScheduleTicks, [](PhaseContext& ctx) {
        ctx.Runtime->ScheduleFixedTicks();
    });
    driver.Register(FramePhase::Simulate, [&](PhaseContext&) { ++ticksSeen; });

    clock.Advance(0.0);
    driver.StepOnce();

    // A stalled frame catches up to the cap and drops the rest, so the frame
    // after a hitch is not itself late.
    clock.Advance(1.0);
    driver.StepOnce();

    EXPECT_EQ(ticksSeen, 4);
    EXPECT_GT(runtime.GetCurrentFrame().TicksDropped, 0u);
}

TEST(SwapchainRebuildWorker, RunningRequestQueuesFollowUpAndReportsCompletedExtents)
{
    SwapchainRebuildWorker worker;
    std::atomic<int> callbackCount{ 0 };
    std::mutex seenMutex;
    std::vector<WindowExtent> seen;

    worker.Start([&](WindowExtent extent) {
        {
            std::lock_guard<std::mutex> lock(seenMutex);
            seen.push_back(extent);
        }
        const int call = ++callbackCount;
        if (call == 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return true;
    });

    ASSERT_TRUE(worker.Request(WindowExtent{ 1280, 720 }));
    while (callbackCount.load() == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    ASSERT_TRUE(worker.Request(WindowExtent{ 1600, 900 }));

    WindowExtent completed{};
    SwapchainRebuildWorker::PollResult poll = SwapchainRebuildWorker::PollResult::InFlight;
    for (int i = 0; i < 200 && poll != SwapchainRebuildWorker::PollResult::Ready; ++i)
    {
        poll = worker.Poll(&completed);
        if (poll != SwapchainRebuildWorker::PollResult::Ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(poll, SwapchainRebuildWorker::PollResult::Ready);
    EXPECT_EQ(completed.Width, 1280u);
    EXPECT_EQ(completed.Height, 720u);

    poll = SwapchainRebuildWorker::PollResult::InFlight;
    for (int i = 0; i < 200 && poll != SwapchainRebuildWorker::PollResult::Ready; ++i)
    {
        poll = worker.Poll(&completed);
        if (poll != SwapchainRebuildWorker::PollResult::Ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.Stop();

    ASSERT_EQ(poll, SwapchainRebuildWorker::PollResult::Ready);
    EXPECT_EQ(completed.Width, 1600u);
    EXPECT_EQ(completed.Height, 900u);
    ASSERT_EQ(callbackCount.load(), 2);

    std::lock_guard<std::mutex> lock(seenMutex);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].Width, 1280u);
    EXPECT_EQ(seen[1].Width, 1600u);
}

// ---------------------------------------------------------------------------
// RenderPacketDoubleBuffer
// ---------------------------------------------------------------------------

TEST(RenderPacketDoubleBuffer, FlipSwapsReadAndWriteSlots)
{
    RenderPacketDoubleBuffer buffer;
    buffer.WriteSlot().FrameIndex = 1;
    buffer.Flip();
    EXPECT_EQ(buffer.ReadSlot().FrameIndex, 1u);
    buffer.WriteSlot().FrameIndex = 2;
    buffer.Flip();
    EXPECT_EQ(buffer.ReadSlot().FrameIndex, 2u);
}

// ---------------------------------------------------------------------------
// FramePacer
// ---------------------------------------------------------------------------

TEST(FramePacer, DisabledPacerReturnsImmediately)
{
    FramePacer pacer;
    pacer.SetTargetFps(0.0);
    const auto start = std::chrono::steady_clock::now();
    pacer.Wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(5));
}

TEST(FramePacer, EnabledPacerHoldsFrameBudget)
{
    FramePacer pacer;
    pacer.SetTargetFps(200.0);  // 5ms frame budget
    pacer.Resync();
    const auto start = std::chrono::steady_clock::now();
    pacer.Wait();
    pacer.Wait();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // Two frames @ 200fps = 10ms. Allow wiggle for OS scheduling.
    EXPECT_GE(elapsed, std::chrono::milliseconds(8));
    EXPECT_LT(elapsed, std::chrono::milliseconds(40));
}
