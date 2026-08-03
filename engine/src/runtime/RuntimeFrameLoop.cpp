#include <runtime/RuntimeFrameLoop.h>

RuntimeFrameSnapshot RuntimeFrameLoop::BeginFrame()
{
    const FrameClock frameClock = WallClock.Advance();
    Current = {};
    Current.WallTime = frameClock;
    Current.TickDtSeconds = SimulationClock.GetFixedDt();
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

TickBudget RuntimeFrameLoop::ScheduleFixedTicks()
{
    // Reset before this frame's time is accumulated: a discontinuity means the
    // elapsed wall time does not correspond to simulated time the new state
    // should replay.
    if (DiscontinuityPending)
    {
        ApplyDiscontinuity();
        Scheduler.Reset();
    }

    // Lifecycle-only frames render nothing and simulate nothing, and the wall
    // time they span belongs to the stall rather than to gameplay. They also
    // end in a discontinuity, which clears the residual anyway.
    if (Current.LifecycleOnly)
    {
        Current.Budget.TicksToRunThisFrame = 0u;
        return Current.Budget;
    }

    const double elapsed =
        static_cast<double>(Current.WallTime.Dt) * static_cast<double>(SimulationTimescale);
    const FixedStepPlan plan = Scheduler.Advance(elapsed, SimulationClock.GetFixedDt());

    Current.Budget.TicksToRunThisFrame = plan.TicksToRun;
    Current.TicksDropped = plan.TicksDropped;
    return Current.Budget;
}

bool RuntimeFrameLoop::CanRunFixedTickThisFrame() const
{
    return Current.FixedTicks < Current.Budget.TicksToRunThisFrame;
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
    // How far presentation sits past the last completed tick. Renderers that
    // keep per-tick history blend with it; everything else renders the latest
    // completed state and ignores it.
    Current.Presentation =
        SimulationClock.BuildPresentationTime(Scheduler.GetAlpha(SimulationClock.GetFixedDt()));
    Current.Presentation.FrameIndex = Current.WallTime.FrameIndex;
    return Current.Presentation;
}

void RuntimeFrameLoop::EndFrame()
{
    if (State == RuntimeFrameState::RecoveringPresentation)
        State = RuntimeFrameState::Running;
    Current.State = State;
    PendingDiscontinuityReason = TemporalDiscontinuityReason::None;
    DiscontinuityPending = false;
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
    Current.Events |= RuntimeFrameEventFlags::TemporalDiscontinuity;
    DiscontinuityPending = false;

    DiscontinuityBus.Publish(FrameDiscontinuityEvent{
        .Reason = Current.DiscontinuityReason,
        .FrameIndex = Current.WallTime.FrameIndex,
    });
}
