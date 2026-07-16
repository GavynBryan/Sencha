#include <render/SpotShadowRenderFeature.h>

#include <core/logging/Logger.h>

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
    Pass.Draw(frame, Lights, Residency.ScheduledViews(), Casters, Meshes, &Residency);
}

void SpotShadowRenderFeature::Teardown()
{
    Pass.Teardown();
    Bindings->Teardown();
}
