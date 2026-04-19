#pragma once

#include <input/InputFrame.h>
#include <runtime/FramePacer.h>
#include <runtime/FrameTrace.h>
#include <runtime/RenderPacket.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/TimingHistory.h>
#include <world/registry/FrameRegistryView.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class FrameDriver;

//=============================================================================
// FramePhase
//
// Named points in the frame pipeline where systems register callbacks. The
// driver invokes each phase's callbacks in registration order. Ordering
// between phases is fixed - this is the engine's contract for how a frame
// is constructed.
//=============================================================================
enum class FramePhase : int
{
    PumpPlatform = 0,         // Poll OS events, populate InputFrame
    ResolveLifecycle = 1,     // Apply window events to RuntimeFrameLoop
    RebuildGraphics = 2,      // Recreate swapchain / swap device resources
    ScheduleTicks = 3,        // Resolve presentation resets and fixed tick budget
    Simulate = 4,             // Fixed-step: gameplay + propagation (0..N calls)
    Update = 5,               // Per-frame game update: camera, HUD, input reactions
    ExtractRenderPacket = 6,  // Build RenderPacket for this frame
    Render = 7,               // Submit + present
    EndFrame = 8,             // Flip buffers, pace, stamp telemetry
    Count,
};

[[nodiscard]] const char* ToString(FramePhase phase);

struct PhaseContext
{
    FrameDriver* Driver = nullptr;
    RuntimeFrameLoop* Runtime = nullptr;
    InputFrame* Input = nullptr;
    RenderPacket* PacketWrite = nullptr;
    RenderPacket* PacketRead = nullptr;
    FixedSimTime CurrentTick{};
    FrameRegistryView Registries;
    FrameTrace* Trace = nullptr;
    bool IsFixedTick = false;
};

using FramePhaseCallback = std::function<void(PhaseContext&)>;

//=============================================================================
// FrameDriver
//
// Owns the outer loop and drives each registered system through the phase
// pipeline once per wall-clock frame. Simulation phase callbacks run zero
// or more times per frame according to RuntimeFrameLoop's tick budget; they
// receive a FrameContext with IsFixedTick = true and a valid CurrentTick.
//
// Design goals:
//   - Application code never writes while(running). It registers callbacks
//     into phases and calls Run().
//   - Phase ordering is an engine-wide invariant, not per-app code.
//   - Lifecycle-only frames (resize, minimize, swapchain rebuild) skip
//     Extract/Render but still pump platform + telemetry.
//   - Simulation consumes FixedSimTime only; wall time is diagnostic telemetry.
//
// Usage:
//   FrameDriver driver(runtime);
//   driver.Register(FramePhase::PumpPlatform, [&](FrameContext& ctx){ ... });
//   driver.Register(FramePhase::Simulate, [&](FrameContext& ctx){ ... });
//   driver.Register(FramePhase::Render, [&](FrameContext& ctx){ ... });
//   driver.Run();  // returns when the shouldExit predicate fires
//=============================================================================
class FrameDriver
{
public:
    explicit FrameDriver(RuntimeFrameLoop& runtime);

    // Register a phase callback. Runs in insertion order within the phase.
    void Register(FramePhase phase, FramePhaseCallback callback);

    // Predicate polled after each PumpPlatform; returning true exits the loop.
    void SetShouldExit(std::function<bool()> predicate) { ShouldExitPredicate = std::move(predicate); }

    // Optional soft frame-rate target. 0.0 = uncapped (present mode decides).
    // Used when swapchain is IMMEDIATE or MAILBOX to avoid burning CPU.
    void SetTargetFps(double fps);
    [[nodiscard]] double GetTargetFps() const { return Pacer.GetTargetFps(); }

    // Optional telemetry sinks. TimingHistory is for the debug HUD.
    // FrameTrace receives phase-boundary markers for Chrome/Tracy export.
    void SetTimingHistory(TimingHistory* history) { History = history; }
    void SetTrace(FrameTrace* trace) { Trace = trace; }

    // Run the loop until the exit predicate fires.
    void Run();

    // Single-frame step for tests.
    void StepOnce();

    [[nodiscard]] RuntimeFrameLoop& GetRuntime() { return Runtime; }
    [[nodiscard]] const InputFrame& GetInputFrame() const { return Input; }
    [[nodiscard]] InputFrame& GetInputFrame() { return Input; }
    [[nodiscard]] RenderPacketDoubleBuffer& GetPacketBuffer() { return Packets; }

    // Simulation consumers typically call this on their first tick per frame.
    // After the call, edge lists are empty for the rest of the frame.
    void DrainInputEdgesForFirstTick();

private:
    void InvokePhase(FramePhase phase, PhaseContext& ctx);

    RuntimeFrameLoop& Runtime;
    FramePacer Pacer;
    InputFrame Input;
    RenderPacketDoubleBuffer Packets;
    TimingHistory* History = nullptr;
    FrameTrace* Trace = nullptr;
    std::function<bool()> ShouldExitPredicate;
    std::vector<FramePhaseCallback> Phases[static_cast<int>(FramePhase::Count)];
    bool EdgesDrainedThisFrame = false;
};
