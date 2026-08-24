#include <render/feature/SkyRenderFeature.h>

#include <render/CameraProjection.h>

SkyRenderFeature::SkyRenderFeature(const CameraRenderData& camera,
                                   const RenderLightSet& lights)
    : Camera(&camera)
    , Lights(&lights)
{
}

bool SkyRenderFeature::Setup(const RenderFeatureServices& services)
{
    Pass.Setup(*services.Backend);
    // A background that fails to compile leaves the host's clear showing, which
    // is what shipped before this feature existed. That is a degraded frame,
    // not an illegal one, so the feature stays registered.
    return true;
}

void SkyRenderFeature::OnDraw(const RenderFrame& frame)
{
    if (!Lights->SkyEnabled)
        return;

    Pass.Draw(*frame.Backend,
              MakeInverseSkyViewProjection(Camera->View, Camera->Projection),
              SkyGradientParams{ .Top = Lights->AmbientSky,
                                 .Bottom = Lights->AmbientGround,
                                 .Exposure = Lights->Exposure,
                                 .TonemapKnee = Lights->TonemapKnee,
                                 .TonemapEnabled = Lights->TonemapEnabled });
}

void SkyRenderFeature::Teardown()
{
    Pass.Teardown();
}
