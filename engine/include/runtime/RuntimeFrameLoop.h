#pragma once

#include <platform/WindowTypes.h>
#include <runtime/FrameDiscontinuityBus.h>
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

//=============================================================================
// EngineClock
//
// Converts a raw wall-clock delta into a sanitized engine delta by clamping
// and zeroing across discontinuities. The sanitized delta is the only time
// value that should reach simulation, animation, or any accumulator.
//=============================================================================
class EngineClock
{
public:
    [[nodiscard]] EngineFrameTime Consume(const PlatformFrameTime& platformTime,
                                          bool temporalDiscontinuity,
                                          bool lifecycleOnly) const
    {
        double dt = platformTime.RawDeltaSeconds;
        if (dt < 0.0) dt = 0.0;
        if (dt > MaxDeltaSeconds) dt = MaxDeltaSeconds;
        if (temporalDiscontinuity || lifecycleOnly)
            dt = 0.0;

        return EngineFrameTime{
            .SanitizedDeltaSeconds = dt,
            .IsTemporalDiscontinuity = temporalDiscontinuity,
            .FrameIndex = platformTime.FrameIndex,
        };
    }

    void SetMaxDeltaSeconds(double seconds) { MaxDeltaSeconds = seconds; }
    [[nodiscard]] double GetMaxDeltaSeconds() const { return MaxDeltaSeconds; }

private:
    double MaxDeltaSeconds = 1.0 / 15.0;
};

struct RuntimeFrameSnapshot
{
    PlatformFrameTime PlatformTime;
    EngineFrameTime EngineTime;
    PresentationTime Presentation;
    double AccumulatorBeforeTicks = 0.0;
    double AccumulatorAfterTicks = 0.0;
    uint32_t FixedTicks = 0;
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

    EngineFrameTime AdvanceEngineTime();
    void AccumulateSimulationTime();
    [[nodiscard]] bool CanRunFixedTickThisFrame() const;
    [[nodiscard]] FixedSimTime BeginFixedTick();
    void EndFixedTick();
    PresentationTime BuildPresentationFrame();
    void EndFrame();

    [[nodiscard]] const RuntimeFrameSnapshot& GetCurrentFrame() const { return Current; }
    [[nodiscard]] RuntimeFrameState GetState() const { return State; }
    [[nodiscard]] FixedSimulationLoop& GetSimulationClock() { return SimulationClock; }
    [[nodiscard]] const FixedSimulationLoop& GetSimulationClock() const { return SimulationClock; }
    [[nodiscard]] TimeService& GetPlatformClock() { return PlatformClock; }
    [[nodiscard]] const TimeService& GetPlatformClock() const { return PlatformClock; }
    [[nodiscard]] FrameDiscontinuityBus& GetDiscontinuityBus() { return DiscontinuityBus; }
    [[nodiscard]] const FrameDiscontinuityBus& GetDiscontinuityBus() const { return DiscontinuityBus; }

    // Deprecated — use GetDiscontinuityBus().Subscribe() instead. This single-
    // consumer flag still exists for code paths that need a cheap synchronous
    // query in the same frame the discontinuity was applied.
    [[nodiscard]] bool ConsumePresentationHistoryReset();

    // Scale engine delta before it is accumulated into the fixed loop.
    // 0.0 fully pauses simulation; 1.0 is realtime. Presentation clock is
    // unaffected so UI continues to update while paused.
    void SetSimulationTimescale(float scale) { SimulationTimescale = scale < 0.0f ? 0.0f : scale; }
    [[nodiscard]] float GetSimulationTimescale() const { return SimulationTimescale; }

    // Resize events can arrive sparsely while the OS is in a live-resize drag.
    // Hold lifecycle mode for a short real-time quiet window so swapchain
    // rebuild/render/re-invalidated cycles do not leak sawtooth engine dt.
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

    TimeService PlatformClock;
    EngineClock SafeEngineClock;
    FixedSimulationLoop SimulationClock;
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
    bool PresentationHistoryResetPending = false;
    TemporalDiscontinuityReason PendingDiscontinuityReason = TemporalDiscontinuityReason::None;
};
