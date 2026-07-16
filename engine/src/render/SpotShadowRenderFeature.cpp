#include <render/SpotShadowRenderFeature.h>

#include <core/logging/Logger.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <graphics/vulkan/GpuTimestampPool.h>
#include <graphics/vulkan/VulkanDebugLabels.h>
#endif

SpotShadowRenderFeature::SpotShadowRenderFeature(
    std::shared_ptr<LightBindings> bindings,
    const RenderLightSet& lights,
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

void SpotShadowRenderFeature::Setup(const RendererServices& services)
{
    Logger* log = services.Logging != nullptr
        ? &services.Logging->GetLogger<SpotShadowRenderFeature>()
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

    Pass.Setup(services, *Bindings);
}

void SpotShadowRenderFeature::OnDraw(const FrameContext& frame)
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    GpuTimestampPool* gpuScopes = Instrumentation != nullptr
        ? Instrumentation->GpuTimestamps
        : nullptr;
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.Cmd, ToString(GpuScope::ShadowSpotViews));
        gpuScopes->BeginScope(frame.Cmd, GpuScope::ShadowSpotViews);
    }
#endif
    Pass.Draw(frame, Lights, Residency.ScheduledViews(), Casters, Meshes, &Residency);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.Cmd, GpuScope::ShadowSpotViews);
        VulkanDebugLabels::EndLabel(frame.Cmd);
    }
#endif

    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
    {
        const SpotShadowDepthPass::DrawStats stats = Pass.GetLastDrawStats();
        Instrumentation->Stats->ShadowViewsRendered = stats.ViewsRendered;
        Instrumentation->Stats->ShadowCasterDraws = stats.CasterDraws;
    }
}

void SpotShadowRenderFeature::Teardown()
{
    Pass.Teardown();
    Bindings->Teardown();
}
