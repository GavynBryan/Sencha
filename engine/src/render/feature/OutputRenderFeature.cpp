#include <render/feature/OutputRenderFeature.h>

bool OutputRenderFeature::Setup(const RenderFeatureServices& services)
{
    return Pass.Setup(*services.Backend);
}

void OutputRenderFeature::OnDraw(const RenderFrame& frame)
{
    Pass.Draw(*frame.Backend);
}

void OutputRenderFeature::Teardown()
{
    Pass.Teardown();
}
