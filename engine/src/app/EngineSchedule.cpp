#include <app/EngineSchedule.h>

#include <algorithm>

void EngineSchedule::Init()
{
    TopoSort(ZoneResidencyEntries);
    TopoSort(PreSimulateEntries);
    TopoSort(FixedLogicEntries);
    TopoSort(PhysicsEntries);
    TopoSort(PostFixedEntries);
    TopoSort(FrameUpdateEntries);
    TopoSort(PresentationUpdateEntries);
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
    ZoneResidencyEntries.clear();
    PreSimulateEntries.clear();
    FixedLogicEntries.clear();
    PhysicsEntries.clear();
    PostFixedEntries.clear();
    FrameUpdateEntries.clear();
    PresentationUpdateEntries.clear();
    ExtractRenderEntries.clear();
    AudioEntries.clear();
    EndFrameEntries.clear();
    Initialized = false;
}

void EngineSchedule::RunZoneResidency(ZoneResidencyContext& ctx)
{
    Run(ZoneResidencyEntries, ctx);
}

void EngineSchedule::RunPreSimulate(PreSimulateContext& ctx)
{
    Run(PreSimulateEntries, ctx);
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

void EngineSchedule::RunPresentationUpdate(PresentationUpdateContext& ctx)
{
    Run(PresentationUpdateEntries, ctx);
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

namespace
{
    template <typename Entries>
    void ResetEntries(Entries& entries)
    {
        for (auto& entry : entries)
        {
            entry.Calls = 0;
            entry.Nanoseconds = 0;
            entry.WorstNanoseconds = 0;
        }
    }

    template <typename Entries>
    void AppendTimings(const Entries& entries, const char* phase, std::vector<ScheduleSystemTiming>& out)
    {
        for (const auto& entry : entries)
            if (entry.Calls != 0)
                out.push_back({ .System = entry.TypeId.name(),
                                .Phase = phase,
                                .Calls = entry.Calls,
                                .Nanoseconds = entry.Nanoseconds,
                                .WorstNanoseconds = entry.WorstNanoseconds });
    }
}

void EngineSchedule::ResetTimings()
{
    ResetEntries(ZoneResidencyEntries);
    ResetEntries(PreSimulateEntries);
    ResetEntries(FixedLogicEntries);
    ResetEntries(PhysicsEntries);
    ResetEntries(PostFixedEntries);
    ResetEntries(FrameUpdateEntries);
    ResetEntries(PresentationUpdateEntries);
    ResetEntries(ExtractRenderEntries);
    ResetEntries(AudioEntries);
    ResetEntries(EndFrameEntries);
    FrameWork.clear();
}

void EngineSchedule::RecordFrameWork(const char* name, std::uint64_t nanoseconds)
{
    auto found = std::find_if(FrameWork.begin(), FrameWork.end(),
                              [&](const ScheduleSystemTiming& timing) { return timing.System == name; });
    if (found == FrameWork.end())
    {
        FrameWork.push_back({ .System = name, .Phase = "Frame" });
        found = FrameWork.end() - 1;
    }
    ++found->Calls;
    found->Nanoseconds += nanoseconds;
    found->WorstNanoseconds = std::max(found->WorstNanoseconds, nanoseconds);
}

std::vector<ScheduleSystemTiming> EngineSchedule::CollectTimings() const
{
    std::vector<ScheduleSystemTiming> out;
    AppendTimings(ZoneResidencyEntries, "ZoneResidency", out);
    AppendTimings(PreSimulateEntries, "PreSimulate", out);
    AppendTimings(FixedLogicEntries, "FixedLogic", out);
    AppendTimings(PhysicsEntries, "Physics", out);
    AppendTimings(PostFixedEntries, "PostFixed", out);
    AppendTimings(FrameUpdateEntries, "FrameUpdate", out);
    AppendTimings(PresentationUpdateEntries, "PresentationUpdate", out);
    AppendTimings(ExtractRenderEntries, "ExtractRender", out);
    AppendTimings(AudioEntries, "Audio", out);
    AppendTimings(EndFrameEntries, "EndFrame", out);
    out.insert(out.end(), FrameWork.begin(), FrameWork.end());
    return out;
}
