#include <app/EngineSchedule.h>

#include <zone/ZoneRuntime.h>

FrameRegistryView EngineSchedule::BuildFrameView(ZoneRuntime& zones)
{
    return zones.BuildFrameView();
}

void EngineSchedule::Init()
{
    TopoSort(FixedLogicEntries);
    TopoSort(PhysicsEntries);
    TopoSort(PostFixedEntries);
    TopoSort(FrameUpdateEntries);
    TopoSort(ExtractRenderEntries);
    TopoSort(AudioEntries);
    TopoSort(EndFrameEntries);

    for (auto& rec : Records)
        if (rec.InitFn)
            rec.InitFn(rec.Ptr);

    Initialized = true;
}

void EngineSchedule::Shutdown()
{
    for (auto it = Records.rbegin(); it != Records.rend(); ++it)
        if (it->ShutdownFn)
            it->ShutdownFn(it->Ptr);

    for (auto it = Records.rbegin(); it != Records.rend(); ++it)
        if (it->DeleteFn)
            it->DeleteFn(it->Ptr);

    Records.clear();
    TypeIndex.clear();
    FixedLogicEntries.clear();
    PhysicsEntries.clear();
    PostFixedEntries.clear();
    FrameUpdateEntries.clear();
    ExtractRenderEntries.clear();
    AudioEntries.clear();
    EndFrameEntries.clear();
    Initialized = false;
}

void EngineSchedule::RunFixedLogic(FixedLogicContext& ctx)
{
    Run(FixedLogicEntries, ctx);
}

void EngineSchedule::RunPhysics(PhysicsContext& ctx)
{
    Run(PhysicsEntries, ctx);
}

void EngineSchedule::RunPostFixed(PostFixedContext& ctx)
{
    Run(PostFixedEntries, ctx);
}

void EngineSchedule::RunFrameUpdate(FrameUpdateContext& ctx)
{
    Run(FrameUpdateEntries, ctx);
}

void EngineSchedule::RunExtractRender(RenderExtractContext& ctx)
{
    Run(ExtractRenderEntries, ctx);
}

void EngineSchedule::RunAudio(AudioContext& ctx)
{
    Run(AudioEntries, ctx);
}

void EngineSchedule::RunEndFrame(EndFrameContext& ctx)
{
    Run(EndFrameEntries, ctx);
}
