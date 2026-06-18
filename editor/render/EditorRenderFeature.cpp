#include "EditorRenderFeature.h"

#include "PreviewBuffer.h"

#include "../viewport/ViewportLayout.h"

EditorRenderFeature::EditorRenderFeature(ViewportLayout& viewportLayout,
                                         LevelScene& scene,
                                         SelectionService& selection,
                                         PreviewBuffer& preview,
                                         ManipulatorSession& session,
                                         const GridSettings& grid)
    : Layout(viewportLayout)
    , GridCfg(grid)
    , Wireframe(scene, Lines)
    , Visuals(scene, Lines)
    , Highlight(scene, selection, session, Lines)
{
    Wireframe.SetPreviewBuffer(&preview);
}

void EditorRenderFeature::Setup(const RendererServices& services)
{
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Grid.Setup(services);
    Lines.Setup(services);
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

        Grid.DrawViewport(frame.Cmd, *viewport, GridCfg,
                          frame.TargetExtent,
                          frame.TargetFormat,
                          frame.DepthFormat);
        Wireframe.DrawViewport(frame, *viewport);
        Visuals.DrawViewport(frame, *viewport);
        Highlight.DrawViewport(frame, *viewport);
    }
}

void EditorRenderFeature::Teardown()
{
    Grid.Teardown();
    Lines.Teardown();
}
