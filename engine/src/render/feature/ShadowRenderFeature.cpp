#include <render/feature/ShadowRenderFeature.h>

#include <core/logging/Logger.h>
#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>


ShadowRenderFeature::ShadowRenderFeature(
    std::shared_ptr<LightBindings> bindings,
    RenderLightSet& lights,
    const ShadowCasterSet& casters,
    StaticMeshCache& meshes,
    ShadowResidency& residency)
    : Bindings(std::move(bindings))
    , Lights(lights)
    , Casters(casters)
    , Meshes(meshes)
    , Residency(residency)
{
}

bool ShadowRenderFeature::Setup(const RenderFeatureServices& services)
{
    Logger* log = services.Logging != nullptr
        ? &services.Logging->GetLogger<ShadowRenderFeature>()
        : nullptr;

    Instrumentation = services.Instrumentation;
    if (!Bindings->Setup(*services.Backend))
    {
        if (log != nullptr)
            log->Warn("Lighting bindings failed to set up; lit rendering disabled");
        // Deliberate degradation, not a failed feature: the frame still
        // presents without lit shadows.
        return true;
    }
    if (!Bindings->CreateAtlas() && log != nullptr)
        log->Warn("Spot shadow atlas creation failed; spot shadows disabled");
    if (!Bindings->CreateCubePool() && log != nullptr)
        log->Warn("Point shadow cube pool creation failed; point shadows disabled");

    Pass.Setup(*services.Backend, *Bindings);
    return true;
}

void ShadowRenderFeature::OnDraw(const RenderFrame& frame)
{
    BeginGpuScope(frame, GpuScope::ShadowViews);
    {
        CpuScopeTimer timer(
            Instrumentation != nullptr ? Instrumentation->CpuScopes : nullptr,
            CpuScope::ShadowRecord);
        Pass.Draw(*frame.Backend, Lights, Residency.ScheduledViews(),
                  Residency.ScheduledPointFaces(), Casters, Meshes, &Residency);
    }
    EndGpuScope(frame, GpuScope::ShadowViews);

    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
    {
        const ShadowDepthPass::DrawStats stats = Pass.GetLastDrawStats();
        Instrumentation->Stats->ShadowViewsRendered =
            stats.ViewsRendered + stats.PointFacesRendered;
        Instrumentation->Stats->PointShadowFacesRendered = stats.PointFacesRendered;
        Instrumentation->Stats->ShadowCasterDraws = stats.CasterDraws;
        Instrumentation->Stats->ShadowCastersTested = stats.CastersTested;
        Instrumentation->Stats->ShadowCastersVisible = stats.CastersVisible;
        Instrumentation->Stats->ShadowCastersDropped = stats.CastersDropped;
        Instrumentation->Stats->ShadowInstanceRuns = stats.InstanceRuns;
        if (stats.Skipped)
            ++Instrumentation->Stats->PassesSkipped;
    }
}

void ShadowRenderFeature::Teardown()
{
    Pass.Teardown();
    Bindings->Teardown();
}
