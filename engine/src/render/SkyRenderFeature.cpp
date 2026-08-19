#include <render/SkyRenderFeature.h>

#include <render/CameraProjection.h>

SkyRenderFeature::SkyRenderFeature(const CameraRenderData& camera,
                                   const RenderLightSet& lights)
    : Camera(&camera)
    , Lights(&lights)
{
}

bool SkyRenderFeature::Setup(const RendererServices& services)
{
    Pass.Setup(services);
    // A background that fails to compile leaves the host's clear showing, which
    // is what shipped before this feature existed. That is a degraded frame,
    // not an illegal one, so the feature stays registered.
    return true;
}

void SkyRenderFeature::OnDraw(const FrameContext& frame)
{
    if (!Lights->SkyEnabled)
        return;

    Pass.Draw(frame,
              MakeInverseSkyViewProjection(Camera->View, Camera->Projection),
              SkyGradientParams{ .Top = Lights->AmbientSky,
                                 .Bottom = Lights->AmbientGround });
}

void SkyRenderFeature::Teardown()
{
    Pass.Teardown();
}
