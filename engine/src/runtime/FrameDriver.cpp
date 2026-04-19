#include <runtime/FrameDriver.h>

const char* ToString(FramePhase phase)
{
    switch (phase)
    {
    case FramePhase::PumpPlatform: return "PumpPlatform";
    case FramePhase::ResolveLifecycle: return "ResolveLifecycle";
    case FramePhase::RebuildGraphics: return "RebuildGraphics";
    case FramePhase::ScheduleTicks: return "ScheduleTicks";
    case FramePhase::Simulate: return "Simulate";
    case FramePhase::Update: return "Update";
    case FramePhase::ExtractRenderPacket: return "ExtractRenderPacket";
    case FramePhase::Render: return "Render";
    case FramePhase::EndFrame: return "EndFrame";
    case FramePhase::Count: return "Count";
    }
    return "Unknown";
}

FrameDriver::FrameDriver(RuntimeFrameLoop& runtime)
    : Runtime(runtime)
{
}

void FrameDriver::Register(FramePhase phase, FramePhaseCallback callback)
{
    const int idx = static_cast<int>(phase);
    if (idx < 0 || idx >= static_cast<int>(FramePhase::Count))
        return;
    Phases[idx].push_back(std::move(callback));
}

void FrameDriver::SetTargetFps(double fps)
{
    Pacer.SetTargetFps(fps);
}

void FrameDriver::InvokePhase(FramePhase phase, PhaseContext& ctx)
{
    const char* name = ToString(phase);
    if (Trace) Trace->BeginPhase(name);
    const int idx = static_cast<int>(phase);
    for (auto& cb : Phases[idx])
    {
        if (cb) cb(ctx);
    }
    if (Trace) Trace->EndPhase(name);
}

void FrameDriver::DrainInputEdgesForFirstTick()
{
    if (EdgesDrainedThisFrame) return;
    Input.ClearEdges();
    EdgesDrainedThisFrame = true;
}

void FrameDriver::StepOnce()
{
    Runtime.BeginFrame();
    EdgesDrainedThisFrame = false;

    PhaseContext ctx;
    ctx.Driver = this;
    ctx.Runtime = &Runtime;
    ctx.Input = &Input;
    ctx.PacketWrite = &Packets.WriteSlot();
    ctx.PacketRead = &Packets.ReadSlot();
    ctx.Trace = Trace;

    if (Trace) Trace->BeginFrame(Runtime.GetCurrentFrame().WallTime.FrameIndex);

    InvokePhase(FramePhase::PumpPlatform, ctx);

    if ((ShouldExitPredicate && ShouldExitPredicate()) || Input.QuitRequested)
    {
        InvokePhase(FramePhase::EndFrame, ctx);
        if (Trace) Trace->EndFrame(Runtime.GetCurrentFrame().WallTime.FrameIndex);
        Runtime.EndFrame();
        Packets.Flip();
        Pacer.WaitForLifecycleIdle();
        return;
    }

    InvokePhase(FramePhase::ResolveLifecycle, ctx);
    InvokePhase(FramePhase::RebuildGraphics, ctx);
    InvokePhase(FramePhase::ScheduleTicks, ctx);

    // Fixed-step simulation loop. Each fixed tick is its own mini-phase so
    // registered simulation callbacks can observe tick count cleanly.
    if (Trace) Trace->BeginPhase("Simulate");
    while (Runtime.CanRunFixedTickThisFrame())
    {
        const bool shouldDrainEdgesAfterTick = !EdgesDrainedThisFrame;
        ctx.CurrentTick = Runtime.BeginFixedTick();
        ctx.IsFixedTick = true;
        const int simIdx = static_cast<int>(FramePhase::Simulate);
        for (auto& cb : Phases[simIdx])
        {
            if (cb) cb(ctx);
        }
        if (shouldDrainEdgesAfterTick)
            DrainInputEdgesForFirstTick();
        Runtime.EndFixedTick();
    }
    ctx.IsFixedTick = false;
    if (Trace) Trace->EndPhase("Simulate");

    Runtime.BuildPresentationFrame();

    InvokePhase(FramePhase::Update, ctx);

    // If no fixed tick ran this frame and edges are still pending, presentation
    // may still want them (mouse-look, etc.) but simulation did not consume
    // them — keep edges alive for the next frame by NOT draining.

    ctx.PacketWrite->Reset();
    ctx.PacketWrite->FrameIndex = Runtime.GetCurrentFrame().WallTime.FrameIndex;
    ctx.PacketWrite->Presentation = Runtime.GetCurrentFrame().Presentation;

    const bool lifecycleOnly = Runtime.GetCurrentFrame().LifecycleOnly;
    if (!lifecycleOnly)
    {
        InvokePhase(FramePhase::ExtractRenderPacket, ctx);
        InvokePhase(FramePhase::Render, ctx);
    }

    InvokePhase(FramePhase::EndFrame, ctx);

    if (Trace) Trace->EndFrame(Runtime.GetCurrentFrame().WallTime.FrameIndex);

    Runtime.EndFrame();
    Packets.Flip();

    if (lifecycleOnly)
    {
        // No swapchain wait happened — don't spin the CPU.
        Pacer.WaitForLifecycleIdle();
    }
    else
    {
        Pacer.Wait();
    }
}

void FrameDriver::Run()
{
    while (true)
    {
        StepOnce();
        if (ShouldExitPredicate && ShouldExitPredicate())
            break;
        if (Input.QuitRequested)
            break;
    }
}
