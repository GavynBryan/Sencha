#include <render/feature/OutputRenderFeature.h>

bool OutputRenderFeature::Setup(const RenderFeatureServices& services)
{
    return Pass.Setup(*services.Backend);
}

void OutputRenderFeature::OnDraw(const RenderFrame& frame)
{
    Pass.Draw(*frame.Backend, *Params);
}

void OutputRenderFeature::Teardown()
{
    Pass.Teardown();
}
