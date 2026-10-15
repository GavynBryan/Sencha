#include <render/feature/DebugLineRenderFeature.h>

DebugLineRenderFeature::DebugLineRenderFeature(const CameraRenderData& camera, const DebugLineFrame& lines)
    : Camera(&camera)
    , Lines(&lines)
{
}

bool DebugLineRenderFeature::Setup(const RenderFeatureServices& services)
{
    Pass.Setup(*services.Backend);
    return true;
}

void DebugLineRenderFeature::OnDraw(const RenderFrame& frame)
{
    if (Lines->Tested.empty() && Lines->OnTop.empty())
        return;
    Pass.Draw(*frame.Backend, *Camera, *Lines);
}

void DebugLineRenderFeature::Teardown()
{
    Pass.Teardown();
}
