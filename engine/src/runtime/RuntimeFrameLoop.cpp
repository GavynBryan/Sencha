#include <runtime/RuntimeFrameLoop.h>

RuntimeFrameSnapshot RuntimeFrameLoop::BeginFrame()
{
    const FrameClock frameClock = PlatformClock.Advance();
    Current = {};
    Current.PlatformTime = PlatformFrameTime{
        .RawDeltaSeconds = frameClock.UnscaledDt,
        .FrameStartSeconds = frameClock.UnscaledElapsed,
        .FrameIndex = frameClock.FrameIndex,
    };
    Current.State = State;
    Current.DiscontinuityReason = PendingDiscontinuityReason;
    return Current;
}

void RuntimeFrameLoop::NotifyResize(WindowExtent extent)
{
    DesiredExtent = extent;
    SwapchainDirty = true;
    ResizeSettleFrames = 2;
    ResizeSettleUntil = Clock::now()
        + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(ResizeSettleSeconds));
    State = Minimized ? RuntimeFrameState::Minimized : RuntimeFrameState::Resizing;
    Current.Events |= RuntimeFrameEventFlags::Resize;
    MarkTemporalDiscontinuity(TemporalDiscontinuityReason::Resize);
}

void RuntimeFrameLoop::NotifyMinimized()
{
    Minimized = true;
    State = RuntimeFrameState::Minimized;
    Current.Events |= RuntimeFrameEventFlags::Minimized;
    MarkTemporalDiscontinuity(TemporalDiscontinuityReason::Minimized);
}

void RuntimeFrameLoop::NotifyRestored(WindowExtent extent)
{
    Minimized = false;
    DesiredExtent = extent;
    SwapchainDirty = IsValidExtent(extent);
    State = SwapchainDirty ? RuntimeFrameState::SwapchainInvalid : RuntimeFrameState::RecoveringPresentation;
    Current.Events |= RuntimeFrameEventFlags::Restored;
    MarkTemporalDiscontinuity(TemporalDiscontinuityReason::Restored);
}

void RuntimeFrameLoop::NotifySwapchainInvalidated()
{
    SwapchainDirty = true;
    State = Minimized ? RuntimeFrameState::Minimized : RuntimeFrameState::SwapchainInvalid;
    Current.Events |= RuntimeFrameEventFlags::SwapchainInvalidated;
    MarkTemporalDiscontinuity(TemporalDiscontinuityReason::SwapchainInvalidated);
}

void RuntimeFrameLoop::MarkTemporalDiscontinuity(TemporalDiscontinuityReason reason)
{
    DiscontinuityPending = true;
    PendingDiscontinuityReason = reason;
    Current.DiscontinuityReason = reason;
    Current.Events |= RuntimeFrameEventFlags::TemporalDiscontinuity;
}

void RuntimeFrameLoop::ResolveLifecycleTransitions()
{
    if (ResizeSettleFrames > 0)
        --ResizeSettleFrames;

    if (Minimized)
    {
        State = RuntimeFrameState::Minimized;
        Current.LifecycleOnly = true;
        Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
    }
    else if (SwapchainDirty)
    {
        State = IsResizeSettling()
            ? RuntimeFrameState::Resizing
            : RuntimeFrameState::SwapchainInvalid;
        Current.LifecycleOnly = true;
        Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
    }
    else if (State == RuntimeFrameState::RecoveringPresentation)
    {
        Current.LifecycleOnly = true;
        Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
    }

    Current.State = State;
}

bool RuntimeFrameLoop::ShouldRebuildSwapchain() const
{
    return SwapchainDirty && !Minimized && !IsResizeSettling() && IsValidExtent(DesiredExtent);
}

void RuntimeFrameLoop::BeginSwapchainRebuild()
{
    State = RuntimeFrameState::RebuildingSwapchain;
    Current.State = State;
    Current.LifecycleOnly = true;
    Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
}

void RuntimeFrameLoop::CompleteSwapchainRebuild(WindowExtent rebuiltExtent)
{
    const bool rebuiltDesiredExtent = ExtentsEqual(rebuiltExtent, DesiredExtent);
    SwapchainDirty = !rebuiltDesiredExtent;
    State = rebuiltDesiredExtent
        ? RuntimeFrameState::RecoveringPresentation
        : (Minimized ? RuntimeFrameState::Minimized : RuntimeFrameState::SwapchainInvalid);
    Current.State = State;
    Current.LifecycleOnly = !rebuiltDesiredExtent;
    if (Current.LifecycleOnly)
        Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
    Current.Events |= RuntimeFrameEventFlags::SwapchainRecreated;
    MarkTemporalDiscontinuity(TemporalDiscontinuityReason::SwapchainRecreated);
    ApplyDiscontinuity();
}

void RuntimeFrameLoop::FailSwapchainRebuild()
{
    State = RuntimeFrameState::SwapchainInvalid;
    Current.State = State;
    Current.LifecycleOnly = true;
    Current.Events |= RuntimeFrameEventFlags::LifecycleOnly;
}

EngineFrameTime RuntimeFrameLoop::AdvanceEngineTime()
{
    if (DiscontinuityPending)
        ApplyDiscontinuity();

    Current.EngineTime = SafeEngineClock.Consume(
        Current.PlatformTime,
        Current.DiscontinuityReason != TemporalDiscontinuityReason::None,
        Current.LifecycleOnly);
    return Current.EngineTime;
}

void RuntimeFrameLoop::AccumulateSimulationTime()
{
    Current.AccumulatorBeforeTicks = SimulationClock.GetAccumulator();
    if (Current.LifecycleOnly)
        return;

    EngineFrameTime scaled = Current.EngineTime;
    scaled.SanitizedDeltaSeconds *= static_cast<double>(SimulationTimescale);
    SimulationClock.Accumulate(scaled);
}

bool RuntimeFrameLoop::CanRunFixedTickThisFrame() const
{
    return !Current.LifecycleOnly
        && Current.FixedTicks < SimulationClock.GetMaxTicksPerFrame()
        && SimulationClock.HasFixedTick();
}

FixedSimTime RuntimeFrameLoop::BeginFixedTick()
{
    return SimulationClock.BeginFixedTick();
}

void RuntimeFrameLoop::EndFixedTick()
{
    SimulationClock.EndFixedTick();
    ++Current.FixedTicks;
}

PresentationTime RuntimeFrameLoop::BuildPresentationFrame()
{
    Current.AccumulatorAfterTicks = SimulationClock.GetAccumulator();
    const bool discontinuity =
        Current.DiscontinuityReason != TemporalDiscontinuityReason::None;
    Current.Presentation = SimulationClock.BuildPresentationTime(
        Current.EngineTime.SanitizedDeltaSeconds);
    Current.Presentation.FrameIndex = Current.PlatformTime.FrameIndex;
    if (discontinuity)
        Current.Presentation.Alpha = 0.0;
    return Current.Presentation;
}

void RuntimeFrameLoop::EndFrame()
{
    // A lifecycle-only frame observes wall-clock time but does not simulate.
    // Resetting the platform clock here ensures the next *rendered* frame does
    // not pay for time spent stalled in resize drags, swapchain rebuilds, or
    // minimized states — the accumulated stall would otherwise clamp to
    // MaxFrameDt, fire multiple fixed ticks, and jitter the camera.
    if (Current.LifecycleOnly)
        PlatformClock.ResetToNow();

    if (State == RuntimeFrameState::RecoveringPresentation)
        State = RuntimeFrameState::Running;
    Current.State = State;
    PendingDiscontinuityReason = TemporalDiscontinuityReason::None;
    DiscontinuityPending = false;
}

bool RuntimeFrameLoop::ConsumePresentationHistoryReset()
{
    const bool reset = PresentationHistoryResetPending;
    PresentationHistoryResetPending = false;
    return reset;
}

void RuntimeFrameLoop::SetResizeSettleSeconds(double seconds)
{
    ResizeSettleSeconds = seconds < 0.0 ? 0.0 : seconds;
}

bool RuntimeFrameLoop::IsResizeSettling() const
{
    return ResizeSettleFrames > 0 || Clock::now() < ResizeSettleUntil;
}

void RuntimeFrameLoop::ApplyDiscontinuity()
{
    PlatformClock.ResetToNow();
    SimulationClock.ResetAfterDiscontinuity();
    PresentationHistoryResetPending = true;
    Current.Events |= RuntimeFrameEventFlags::TemporalDiscontinuity;
    DiscontinuityPending = false;

    DiscontinuityBus.Publish(FrameDiscontinuityEvent{
        .Reason = Current.DiscontinuityReason,
        .FrameIndex = Current.PlatformTime.FrameIndex,
    });
}
