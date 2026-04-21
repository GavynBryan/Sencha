#include "EditorRenderFeature.h"

#include "../viewport/FourWayViewportLayout.h"

EditorRenderFeature::EditorRenderFeature(FourWayViewportLayout& viewportLayout)
    : ViewportLayout(viewportLayout)
{
}

void EditorRenderFeature::Setup(const RendererServices& services)
{
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Grid.Setup(services);
    if (Log != nullptr)
        Log->Info("EditorRenderFeature setup complete");
}

void EditorRenderFeature::OnDraw(const FrameContext& frame)
{
    if (!LoggedFirstDraw && Log != nullptr)
    {
        Log->Info("EditorRenderFeature drawing");
        LoggedFirstDraw = true;
    }

    for (EditorViewport& viewport : ViewportLayout.GetViewports())
    {
        Grid.DrawViewport(frame.Cmd, viewport,
                          frame.TargetExtent,
                          frame.TargetFormat,
                          frame.DepthFormat);
    }
}

void EditorRenderFeature::Teardown()
{
    Grid.Teardown();
}
