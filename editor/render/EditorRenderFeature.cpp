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
    Backdrop.Setup(services);
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

    const auto drawViewport = [&](EditorViewport& viewport)
    {
        Backdrop.DrawViewport(frame.Cmd, viewport, frame.TargetExtent,
                              frame.TargetFormat, frame.DepthFormat);
        Grid.DrawViewport(frame.Cmd, viewport, GridCfg,
                          frame.TargetExtent,
                          frame.TargetFormat,
                          frame.DepthFormat);
        Wireframe.DrawViewport(frame, viewport);
        Visuals.DrawViewport(frame, viewport);
        Highlight.DrawViewport(frame, viewport);
    };

    // Render only what the panel lays out: every leaf in quad mode, just the
    // active viewport in single mode (the others hold stale screen rects).
    if (Layout.GetMode() == LayoutMode::Single)
    {
        if (EditorViewport* active = Layout.Active())
            drawViewport(*active);
        return;
    }

    for (const auto& viewport : Layout.All())
    {
        if (viewport != nullptr)
            drawViewport(*viewport);
    }
}

void EditorRenderFeature::Teardown()
{
    Backdrop.Teardown();
    Grid.Teardown();
    Lines.Teardown();
}
