#pragma once

#include <input/InputFrame.h>
#include <runtime/FramePacer.h>
#include <runtime/FrameTrace.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

//=============================================================================
// FramePhase
//=============================================================================
enum class FramePhase : int
{
    // Blocks until the GPU has a frame slot free and the display queue is within
    // its limit. First, so a frame that waits does so before sampling input.
    AwaitFrameSlot = 0,
    PumpPlatform = 1,
    ResolveLifecycle = 2,
    RebuildGraphics = 3,
    DrainAsyncTasks = 4,
    // Before ZoneResidency, so a zone grant that arrives this frame reaches
    // residency processing in the same frame rather than the next one, and
    // before ScheduleTicks, because the tick budget consumes the clock-sync
    // estimate this phase updates.
    PumpNet = 5,
    ZoneResidency = 6,
    ScheduleTicks = 7,
    Simulate = 8,
    // After the ticks, so a snapshot leaves in the frame that produced it, and
    // before presentation has consumed anything.
    FlushNet = 9,
    Update = 10,
    ExtractRender = 11,
    Render = 12,
    EndFrame = 13,
    Count,
};

[[nodiscard]] const char* ToString(FramePhase phase);

struct PhaseContext
{
    RuntimeFrameLoop* Runtime = nullptr;
    InputFrame* Input = nullptr;
    FixedSimTime CurrentTick{};

    // RuntimeWorld-owned scratch built once after zone residency and valid until
    // EndFrame. Phase contexts below receive the appropriate domain set from it.
    const FrameZoneView* Zones = nullptr;

    bool IsFixedTick = false;
};

using FramePhaseCallback = std::function<void(PhaseContext&)>;

class FrameDriver
{
public:
    explicit FrameDriver(RuntimeFrameLoop& runtime);

    void Register(FramePhase phase, FramePhaseCallback callback);

    void SetShouldExit(std::function<bool()> predicate) { ShouldExitPredicate = std::move(predicate); }
    void SetTargetFps(double fps);
    [[nodiscard]] double GetTargetFps() const { return Pacer.GetTargetFps(); }

    void SetTrace(ChromeJsonFrameTrace* trace) { Trace = trace; }

    void Run();
    void StepOnce();

    [[nodiscard]] const InputFrame& GetInputFrame() const { return Input; }
    [[nodiscard]] InputFrame& GetInputFrame() { return Input; }

private:
    void InvokePhase(FramePhase phase, PhaseContext& ctx);

    // Input edges are consumed by the first fixed tick of a frame. A frame that
    // runs no tick leaves them intact so the press reaches the next tick
    // instead of being lost; ticks after the first see held state only.
    void DrainInputEdgesForFirstTick();

    RuntimeFrameLoop& Runtime;
    FramePacer Pacer;
    InputFrame Input;
    ChromeJsonFrameTrace* Trace = nullptr;
    std::function<bool()> ShouldExitPredicate;
    std::vector<FramePhaseCallback> Phases[static_cast<int>(FramePhase::Count)];
    double LimiterWaitSeconds = 0.0;
    bool EdgesDrainedThisFrame = false;
};
