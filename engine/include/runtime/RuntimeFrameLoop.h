#pragma once

#include <platform/WindowTypes.h>
#include <runtime/FrameDiscontinuityBus.h>
#include <time/FixedStepScheduler.h>
#include <time/FrameClock.h>
#include <time/SimClock.h>
#include <time/TimeService.h>

#include <chrono>
#include <cstdint>

enum class RuntimeFrameState
{
    Running,
    Resizing,
    SwapchainInvalid,
    RebuildingSwapchain,
    RecoveringPresentation,
    Minimized,
    Suspended,
};

enum class TemporalDiscontinuityReason
{
    None,
    Resize,
    SwapchainInvalidated,
    SwapchainRecreated,
    Minimized,
    Restored,
    Suspended,
    DebugPause,
    Teleport,
    ZoneLoad,
    RegistryReset,
};

enum class RuntimeFrameEventFlags : uint32_t
{
    None = 0,
    Resize = 1u << 0,
    SwapchainInvalidated = 1u << 1,
    SwapchainRecreated = 1u << 2,
    TemporalDiscontinuity = 1u << 3,
    Minimized = 1u << 4,
    Restored = 1u << 5,
    LifecycleOnly = 1u << 6,
};

inline RuntimeFrameEventFlags operator|(RuntimeFrameEventFlags a, RuntimeFrameEventFlags b)
{
    return static_cast<RuntimeFrameEventFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RuntimeFrameEventFlags& operator|=(RuntimeFrameEventFlags& a, RuntimeFrameEventFlags b)
{
    a = a | b;
    return a;
}

inline bool HasRuntimeFrameEvent(RuntimeFrameEventFlags flags, RuntimeFrameEventFlags flag)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

struct RuntimeFrameSnapshot
{
    FrameClock WallTime;
    PresentationTime Presentation;
    TickBudget Budget;
    double TickDtSeconds = FixedSimulationLoop::DefaultFixedDt;
    uint32_t FixedTicks = 0;

    // Whole ticks this frame's elapsed time covered but the per-frame cap
    // refused to run. Non-zero means simulated time fell behind wall time.
    uint32_t TicksDropped = 0;
    RuntimeFrameState State = RuntimeFrameState::Running;
    TemporalDiscontinuityReason DiscontinuityReason = TemporalDiscontinuityReason::None;
    RuntimeFrameEventFlags Events = RuntimeFrameEventFlags::None;
    bool LifecycleOnly = false;
};

class RuntimeFrameLoop
{
public:
    RuntimeFrameSnapshot BeginFrame();
    void NotifyResize(WindowExtent extent);
    void SetSurfaceExtent(WindowExtent extent) { DesiredExtent = extent; }
    void NotifyMinimized();
    void NotifyRestored(WindowExtent extent);
    void NotifySwapchainInvalidated();
    void MarkTemporalDiscontinuity(TemporalDiscontinuityReason reason);

    void ResolveLifecycleTransitions();
    [[nodiscard]] bool ShouldRebuildSwapchain() const;
    [[nodiscard]] WindowExtent GetDesiredSwapchainExtent() const { return DesiredExtent; }
    void BeginSwapchainRebuild();
    void CompleteSwapchainRebuild(WindowExtent rebuiltExtent);
    void FailSwapchainRebuild();

    TickBudget ScheduleFixedTicks();
    [[nodiscard]] bool CanRunFixedTickThisFrame() const;
    [[nodiscard]] FixedSimTime BeginFixedTick();
    void EndFixedTick();
    PresentationTime BuildPresentationFrame();
    void EndFrame();

    [[nodiscard]] const RuntimeFrameSnapshot& GetCurrentFrame() const { return Current; }
    [[nodiscard]] RuntimeFrameState GetState() const { return State; }
    [[nodiscard]] FixedSimulationLoop& GetSimulationClock() { return SimulationClock; }
    [[nodiscard]] const FixedSimulationLoop& GetSimulationClock() const { return SimulationClock; }
    [[nodiscard]] TimeService& GetWallClock() { return WallClock; }
    [[nodiscard]] const TimeService& GetWallClock() const { return WallClock; }
    [[nodiscard]] FrameDiscontinuityBus& GetDiscontinuityBus() { return DiscontinuityBus; }
    [[nodiscard]] const FrameDiscontinuityBus& GetDiscontinuityBus() const { return DiscontinuityBus; }

    // Scales wall time on its way into the tick accumulator, so it changes how
    // often ticks are emitted and never how long a tick is:
    // FixedSimTime::DeltaSeconds stays constant and per-tick behavior stays
    // deterministic. 0.0 accumulates nothing, which is what pauses simulation.
    void SetSimulationTimescale(float scale) { SimulationTimescale = scale < 0.0f ? 0.0f : scale; }
    [[nodiscard]] float GetSimulationTimescale() const { return SimulationTimescale; }

    // Bounds on catch-up after a stall. Ticks beyond the cap are dropped, not
    // deferred; see FixedStepScheduler.
    void SetMaxFixedTicksPerFrame(uint32_t ticks) { Scheduler.SetMaxTicksPerFrame(ticks); }
    void SetMaxFrameWallDeltaSeconds(double seconds) { Scheduler.SetMaxFrameInputSeconds(seconds); }
    [[nodiscard]] const FixedStepScheduler& GetFixedStepScheduler() const { return Scheduler; }

    // Resize events can arrive sparsely while the OS is in a live-resize drag.
    // Hold lifecycle mode for a short real-time quiet window so swapchain
    // rebuild/render/re-invalidated cycles do not churn graphics resources.
    void SetResizeSettleSeconds(double seconds);
    [[nodiscard]] double GetResizeSettleSeconds() const { return ResizeSettleSeconds; }

private:
    using Clock = std::chrono::steady_clock;

    bool IsValidExtent(WindowExtent extent) const
    {
        return extent.Width != 0 && extent.Height != 0;
    }

    bool ExtentsEqual(WindowExtent a, WindowExtent b) const
    {
        return a.Width == b.Width && a.Height == b.Height;
    }

    [[nodiscard]] bool IsResizeSettling() const;

    void ApplyDiscontinuity();

    TimeService WallClock;
    FixedSimulationLoop SimulationClock;
    // Persistent across frames on purpose: the sub-tick residual is the state
    // that makes tick emission track wall time. Current is cleared every frame.
    FixedStepScheduler Scheduler;
    FrameDiscontinuityBus DiscontinuityBus;
    RuntimeFrameSnapshot Current;
    RuntimeFrameState State = RuntimeFrameState::Running;
    WindowExtent DesiredExtent{};
    uint32_t ResizeSettleFrames = 0;
    double ResizeSettleSeconds = 0.10;
    Clock::time_point ResizeSettleUntil{};
    float SimulationTimescale = 1.0f;
    bool SwapchainDirty = false;
    bool Minimized = false;
    bool DiscontinuityPending = false;
    TemporalDiscontinuityReason PendingDiscontinuityReason = TemporalDiscontinuityReason::None;
};
