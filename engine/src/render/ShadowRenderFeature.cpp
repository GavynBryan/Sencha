#include <render/ShadowRenderFeature.h>

#include <core/logging/Logger.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif

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

void ShadowRenderFeature::Setup(const RendererServices& services)
{
    Logger* log = services.Logging != nullptr
        ? &services.Logging->GetLogger<ShadowRenderFeature>()
        : nullptr;

    Instrumentation = services.Instrumentation;
    if (!Bindings->Setup(services))
    {
        if (log != nullptr)
            log->Warn("Lighting bindings failed to set up; lit rendering disabled");
        return;
    }
    if (!Bindings->CreateAtlas() && log != nullptr)
        log->Warn("Spot shadow atlas creation failed; spot shadows disabled");
    if (!Bindings->CreateCubePool() && log != nullptr)
        log->Warn("Point shadow cube pool creation failed; point shadows disabled");

    Pass.Setup(services, *Bindings);
}

void ShadowRenderFeature::OnDraw(const FrameContext& frame)
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* gpuScopes = Instrumentation != nullptr
        ? Instrumentation->GpuTimestamps
        : nullptr;
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.Cmd, ToString(GpuScope::ShadowViews));
        gpuScopes->BeginScope(frame.Cmd, GpuScope::ShadowViews);
    }
#endif
    Pass.Draw(frame, Lights, Residency.ScheduledViews(),
              Residency.ScheduledPointFaces(), Casters, Meshes, &Residency);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.Cmd, GpuScope::ShadowViews);
        VulkanDebugLabels::EndLabel(frame.Cmd);
    }
#endif

    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
    {
        const ShadowDepthPass::DrawStats stats = Pass.GetLastDrawStats();
        Instrumentation->Stats->ShadowViewsRendered =
            stats.ViewsRendered + stats.PointFacesRendered;
        Instrumentation->Stats->PointShadowFacesRendered = stats.PointFacesRendered;
        Instrumentation->Stats->ShadowCasterDraws = stats.CasterDraws;
    }
}

void ShadowRenderFeature::Teardown()
{
    Pass.Teardown();
    Bindings->Teardown();
}
