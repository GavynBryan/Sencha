#include <render/feature/PostRenderFeature.h>

bool PostRenderFeature::Setup(const RenderFeatureServices& services)
{
    return Chain.Setup(*services.Backend, *Catalog);
}

void PostRenderFeature::OnDraw(const RenderFrame& frame)
{
    Chain.Record(*frame.Backend, *Effects, *Output);
}

void PostRenderFeature::Teardown()
{
    Chain.Teardown();
}
