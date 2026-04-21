#include "EditorRenderFeature.h"

#include "PreviewBuffer.h"

#include "../viewport/ViewportLayout.h"

EditorRenderFeature::EditorRenderFeature(ViewportLayout& viewportLayout,
                                         LevelScene& scene,
                                         SelectionService& selection,
                                         PreviewBuffer& preview)
    : Layout(viewportLayout)
    , Wireframe(scene)
    , Highlight(scene, selection)
{
    Wireframe.SetPreviewBuffer(&preview);
}

void EditorRenderFeature::Setup(const RendererServices& services)
{
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Grid.Setup(services);
    Wireframe.Setup(services);
    Highlight.Setup(services);
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

    for (const auto& viewport : Layout.All())
    {
        if (viewport == nullptr)
            continue;

        Grid.DrawViewport(frame.Cmd, *viewport,
                          frame.TargetExtent,
                          frame.TargetFormat,
                          frame.DepthFormat);
        Wireframe.DrawViewport(frame, *viewport);
        Highlight.DrawViewport(frame, *viewport);
    }
}

void EditorRenderFeature::Teardown()
{
    Grid.Teardown();
    Wireframe.Teardown();
    Highlight.Teardown();
}
