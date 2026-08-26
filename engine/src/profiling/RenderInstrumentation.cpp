#include <profiling/RenderInstrumentation.h>

const char* ToString(RenderProfileMode mode)
{
    switch (mode)
    {
    case RenderProfileMode::Off:      return "off";
    case RenderProfileMode::Counters: return "counters";
    case RenderProfileMode::Gpu:      return "gpu";
    case RenderProfileMode::Capture:  return "capture";
    }
    return "off";
}

const char* ToString(GpuScope scope)
{
    switch (scope)
    {
    case GpuScope::PhaseOffscreen:  return "Phase/Offscreen";
    case GpuScope::PhaseMainColor:  return "Phase/MainColor";
    case GpuScope::ShadowViews: return "Shadow/Views";
    case GpuScope::SkinPose:    return "Skin/Pose";
    case GpuScope::ForwardOpaque:   return "Forward/Opaque";
    case GpuScope::Count:           break;
    }
    return "?";
}

bool ParseRenderProfileMode(std::string_view name, RenderProfileMode& mode)
{
    if (name == "off")
    {
        mode = RenderProfileMode::Off;
        return true;
    }
    if (name == "counters")
    {
        mode = RenderProfileMode::Counters;
        return true;
    }
    if (name == "gpu")
    {
        mode = RenderProfileMode::Gpu;
        return true;
    }
    if (name == "capture")
    {
        mode = RenderProfileMode::Capture;
        return true;
    }
    return false;
}

RenderInstrumentation ResolveInstrumentationBundle(RenderProfileMode active,
                                                   RenderStats* stats,
                                                   RenderStatsHistory* statsHistory,
                                                   CpuScopeTimings* cpuScopes,
                                                   GpuTimestampPool* gpuTimestamps,
                                                   RenderCapture* capture)
{
    const bool counters = active >= RenderProfileMode::Counters;
    RenderInstrumentation bundle;
    bundle.Stats = counters ? stats : nullptr;
    bundle.StatsHistory = counters ? statsHistory : nullptr;
    bundle.CpuScopes = counters ? cpuScopes : nullptr;
    bundle.GpuTimestamps =
        active >= RenderProfileMode::Gpu ? gpuTimestamps : nullptr;
    bundle.Capture = active >= RenderProfileMode::Capture ? capture : nullptr;
    return bundle;
}
