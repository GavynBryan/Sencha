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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
    // Deterministic stepper: advances runtime by one frame of known dt.
    // Avoids relying on wall clock for tick count assertions.
    void StepFrameWithDelta(RuntimeFrameLoop& runtime, double rawDt)
    {
        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        // Inject raw dt directly into the sim accumulator to bypass wall clock.
        runtime.GetSimulationClock().AddFrameDelta(rawDt);
        runtime.AdvanceEngineTime();
        while (runtime.CanRunFixedTickThisFrame())
        {
            (void)runtime.BeginFixedTick();
            runtime.EndFixedTick();
        }
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
    }
}

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

TEST(RuntimeFrameLoopScenario, BurstResizeKeepsAccumulatorClean)
{
    RuntimeFrameLoop runtime;
    runtime.SetResizeSettleSeconds(0.0);
    for (int i = 0; i < 60; ++i)
    {
        runtime.BeginFrame();
        if (i % 3 == 0)
            runtime.NotifyResize(WindowExtent{ 1280u + uint32_t(i), 720u });
        runtime.ResolveLifecycleTransitions();
        runtime.AdvanceEngineTime();
        runtime.AccumulateSimulationTime();
        // Accumulator must never go negative or explode beyond max.
        EXPECT_GE(runtime.GetSimulationClock().GetAccumulator(), 0.0);
        EXPECT_LE(runtime.GetSimulationClock().GetAccumulator(), 0.5);
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
// Scenario: minimized-for-long-time does not explode accumulator
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, LongMinimizeDoesNotAccumulateSimulationTime)
{
    RuntimeFrameLoop runtime;
    runtime.BeginFrame();
    runtime.NotifyMinimized();
    runtime.ResolveLifecycleTransitions();
    runtime.AdvanceEngineTime();
    runtime.AccumulateSimulationTime();
    runtime.EndFrame();

    // Sleep a realistic-ish stall (5ms — enough to exceed fixed dt of ~16.7ms? no,
    // use injected delta instead).
    for (int i = 0; i < 20; ++i)
    {
        runtime.BeginFrame();
        runtime.ResolveLifecycleTransitions();
        runtime.AdvanceEngineTime();
        runtime.AccumulateSimulationTime();
        runtime.BuildPresentationFrame();
        runtime.EndFrame();
        EXPECT_DOUBLE_EQ(runtime.GetSimulationClock().GetAccumulator(), 0.0);
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

    // Burn first frame so subsequent delta is nonzero.
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.AdvanceEngineTime();
    runtime.AccumulateSimulationTime();
    runtime.EndFrame();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    uint32_t fixedTicks = 0;
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.AdvanceEngineTime();
    runtime.AccumulateSimulationTime();
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

    // Directly inject deltas to avoid wall-clock flake in CI.
    uint32_t ticksAt1x = 0;
    runtime.BeginFrame();
    runtime.ResolveLifecycleTransitions();
    runtime.AdvanceEngineTime();
    // Inject 50ms worth of engine time (more than 3 fixed ticks at 60hz).
    runtime.GetSimulationClock().AddFrameDelta(0.05);
    while (runtime.CanRunFixedTickThisFrame())
    {
        (void)runtime.BeginFixedTick();
        runtime.EndFixedTick();
        ++ticksAt1x;
    }
    runtime.BuildPresentationFrame();
    runtime.EndFrame();
    EXPECT_GE(ticksAt1x, 3u);
}

// ---------------------------------------------------------------------------
// Scenario: PresentationHistoryResetPending and bus both fire together
// ---------------------------------------------------------------------------

TEST(RuntimeFrameLoopScenario, DiscontinuityFiresBusAndLegacyFlagOnce)
{
    RuntimeFrameLoop runtime;
    int busCalls = 0;
    runtime.GetDiscontinuityBus().Subscribe(
        [&](const FrameDiscontinuityEvent&) { ++busCalls; });

    runtime.BeginFrame();
    runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::Teleport);
    runtime.AdvanceEngineTime();

    EXPECT_EQ(busCalls, 1);
    EXPECT_TRUE(runtime.ConsumePresentationHistoryReset());
    // Second consume is false — flag was cleared.
    EXPECT_FALSE(runtime.ConsumePresentationHistoryReset());
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

TEST(FrameDriver, FirstFixedTickSeesInputEdgesAndLaterTicksDoNot)
{
    RuntimeFrameLoop runtime;
    FrameDriver driver(runtime);

    int tickCount = 0;
    std::size_t firstTickPressed = 0;
    std::size_t secondTickPressed = 0;

    driver.Register(FramePhase::PumpPlatform, [&](PhaseContext& ctx) {
        ctx.Input->SetKeyHeld(kTestScancode, true);
        ctx.Input->KeysPressed.push_back(kTestScancode);
    });
    driver.Register(FramePhase::AdvanceEngineTime, [&](PhaseContext& ctx) {
        ctx.Runtime->GetSimulationClock().AddFrameDelta(0.05);
    });
    driver.Register(FramePhase::Simulate, [&](PhaseContext& ctx) {
        if (tickCount == 0)
            firstTickPressed = ctx.Input->KeysPressed.size();
        else if (tickCount == 1)
            secondTickPressed = ctx.Input->KeysPressed.size();
        ++tickCount;
    });

    driver.StepOnce();

    EXPECT_GE(tickCount, 2);
    EXPECT_EQ(firstTickPressed, 1u);
    EXPECT_EQ(secondTickPressed, 0u);
    EXPECT_TRUE(driver.GetInputFrame().IsKeyDown(kTestScancode));
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
