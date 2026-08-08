#pragma once

#include <input/InputFrame.h>
#include <runtime/FramePacer.h>
#include <runtime/FrameTrace.h>
#include <runtime/RenderPacket.h>
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
    PumpPlatform = 0,
    ResolveLifecycle = 1,
    RebuildGraphics = 2,
    DrainAsyncTasks = 3,
    // Before ZoneResidency, so a zone grant that arrives this frame reaches
    // residency processing in the same frame rather than the next one, and
    // before ScheduleTicks, because the tick budget consumes the clock-sync
    // estimate this phase updates.
    PumpNet = 4,
    ZoneResidency = 5,
    ScheduleTicks = 6,
    Simulate = 7,
    // After the ticks, so a snapshot leaves in the frame that produced it, and
    // before presentation has consumed anything.
    FlushNet = 8,
    Update = 9,
    ExtractRenderPacket = 10,
    Render = 11,
    EndFrame = 12,
    Count,
};

[[nodiscard]] const char* ToString(FramePhase phase);

struct PhaseContext
{
    RuntimeFrameLoop* Runtime = nullptr;
    InputFrame* Input = nullptr;
    RenderPacket* PacketWrite = nullptr;
    RenderPacket* PacketRead = nullptr;
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
    RenderPacketDoubleBuffer Packets;
    ChromeJsonFrameTrace* Trace = nullptr;
    std::function<bool()> ShouldExitPredicate;
    std::vector<FramePhaseCallback> Phases[static_cast<int>(FramePhase::Count)];
    bool EdgesDrainedThisFrame = false;
};
