#include "EditorRenderFeature.h"

#include "PreviewBuffer.h"

#include "../viewport/ViewportLayout.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

#include <variant>

EditorRenderFeature::EditorRenderFeature(ViewportLayout& viewportLayout,
                                         LevelScene& scene,
                                         SelectionService& selection,
                                         PreviewBuffer& preview,
                                         ManipulatorSession& session,
                                         const GridSettings& grid,
                                         LoggingProvider& logging,
                                         const ConsoleRegistry& console,
                                         AssetSystem* assets,
                                         const AssetRegistry* catalog)
    : Layout(viewportLayout)
    , GridCfg(grid)
    , BrushSolid(scene, Solid)
    , Meshes(scene, Solid, logging, assets, catalog)
    , Wireframe(scene, Lines)
    , Visuals(scene, Lines)
    , Highlight(scene, selection, session, Lines)
    , Preview(preview, Lines)
    , Console(&console)
{
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Wireframe)] = &Wireframe;
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &BrushSolid;
}

void EditorRenderFeature::Setup(const RendererServices& services)
{
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Backdrop.Setup(services);
    Grid.Setup(services);
    Solid.Setup(services);
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

    // Match play-mode backface culling by default; the cvar lets you draw both sides to
    // diagnose inverted/missing-winding geometry. Missing cvar falls back to culling.
    bool cullBackfaces = true;
    if (const CVarMetadata* cvar = Console->FindCVar("editor.cull_backfaces");
        cvar != nullptr && std::holds_alternative<bool>(cvar->CurrentValue))
        cullBackfaces = std::get<bool>(cvar->CurrentValue);
    Solid.SetCullBackfaces(cullBackfaces);

    const auto drawViewport = [&](EditorViewport& viewport)
    {
        Backdrop.DrawViewport(frame.Cmd, viewport, frame.TargetExtent,
                              frame.TargetFormat, frame.DepthFormat);
        Grid.DrawViewport(frame.Cmd, viewport, GridCfg,
                          frame.TargetExtent,
                          frame.TargetFormat,
                          frame.DepthFormat);
        if (IBrushBodyRenderer* body = BodyRenderers[static_cast<std::size_t>(viewport.Shading)])
            body->DrawViewport(frame, viewport);
        // Meshes draw solid in every viewport so a placed mesh reads regardless of
        // the viewport's brush shading.
        Meshes.DrawViewport(frame, viewport);
        Visuals.DrawViewport(frame, viewport);
        Highlight.DrawViewport(frame, viewport);
        Preview.DrawViewport(frame, viewport);
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
    Solid.Teardown();
    Lines.Teardown();
}
