#include "EditorRenderFeature.h"

#include "PreviewBuffer.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"

#include "EditorTheme.h"
#include "viewport/ViewportLayout.h"
#include "viewport/WorldViewSettings.h"
#include "viewport/ViewportShading.h"

#include <app/EngineConsoleBuiltins.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/CVarRead.h>
#include <core/console/ConsoleTypes.h>
#include <render/CameraProjection.h>
#include <render/RenderLightCVars.h>
#include <world/registry/Registry.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanBarriers.h>

#include <optional>
#include <variant>
#include <vector>

EditorRenderFeature::EditorRenderFeature(ViewportLayout& viewportLayout,
                                         WorldDocument& world,
                                         EditorAffordanceService& affordances,
                                         SelectionService& selection,
                                         MeshEditService& meshEdit,
                                         const EditorOverlayState& overlay,
                                         PreviewBuffer& preview,
                                         std::function<const ManipulatorSession*()> session,
                                         const GridSettings& grid,
                                         WorldViewSettings& worldView,
                                         LoggingProvider& logging,
                                         const ConsoleRegistry& console,
                                         AssetSystem* assets,
                                         const AssetRegistry* catalog,
                                         RuntimeAssets* runtimeAssets)
    : World(world)
    , Session(std::move(session))
    , Layout(viewportLayout)
    , GridCfg(grid)
    , WorldView(worldView)
    , BrushSolid(Solid)
    , Meshes(Solid, logging, assets, catalog)
    , Wireframe(selection, overlay, Lines)
    , Visuals(Lines)
    , Highlight(selection, meshEdit, overlay, WideLines, Fills)
    , BrushFills(Fills)
    , ZoneBounds(WideLines)
    , IrradianceVolumes(selection, WideLines, Lines)
    , Affordances(affordances, WideLines, Fills)
    , Preview(preview, Lines)
    , Console(&console)
{
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Wireframe)] = &Wireframe;

    // Real-material WYSIWYG path when an asset environment is present (the editor
    // always has one). The builder + SceneSolidRenderer replace BrushSolid for the
    // Solid body, and the placed-mesh queue replaces the StaticMeshRenderer draw.
    // The IBrushBodyRenderer seam stays at two implementations (Wireframe + SceneSolid).
    RuntimeAssetsRef = runtimeAssets;
    LoggingRef = &logging;
    if (runtimeAssets != nullptr)
    {
        MeshCache = runtimeAssets->StaticMeshes.get();
        MaterialStore = &runtimeAssets->Materials;
        QueueBuilder.emplace(runtimeAssets->Assets, *runtimeAssets->StaticMeshes,
                             runtimeAssets->Materials, runtimeAssets->MaterialSets,
                             logging, runtimeAssets->Textures.get());
        SceneSolid.emplace(Forward, *QueueBuilder, *runtimeAssets->StaticMeshes,
                           runtimeAssets->Materials);
        MaterialPath = true;
        BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &*SceneSolid;
    }
    else
    {
        // No asset environment (brush-only/headless): fall back to the procedural checker.
        BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &BrushSolid;
    }
}

bool EditorRenderFeature::Setup(const RendererServices& services)
{
    Services = services;
    Log = services.Logging ? &services.Logging->GetLogger<EditorRenderFeature>() : nullptr;
    Backdrop.Setup(services);
    Sky.Setup(services);
    Grid.Setup(services);
    Solid.Setup(services);
    if (!Lighting.Setup(services))
    {
        if (Log != nullptr)
            Log->Warn("Lighting bindings failed to set up; forward pass disabled");
    }
    else
    {
        if (!Lighting.CreateAtlas() && Log != nullptr)
            Log->Warn("Spot shadow atlas creation failed; viewport shadows disabled");
        if (!Lighting.CreateCubePool() && Log != nullptr)
            Log->Warn("Point shadow cube pool creation failed; viewport shadows disabled");
        ShadowPass.Setup(services, Lighting);
    }
    // After ShadowPass: the forward pass's frame-UBO range write must be the
    // last one so every dynamic-offset bind covers the largest block.
    Forward.Setup(services, Lighting);
    Lines.Setup(services);
    WideLines.Setup(services);
    Fills.Setup(services);
    Targets.Setup(services);
    Bloom.Setup(services);
    if (Log != nullptr)
        Log->Info("EditorRenderFeature setup complete");
    // Each viewport renderer degrades on its own (a failed lighting set
    // disables the forward pass, not the viewport), so the feature itself is
    // usable whenever it got this far.
    return true;
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
    Solid.SetCullBackfaces(ReadCVarBool(Console, "editor.cull_backfaces", true));

    // Live look knobs via cvars (dial in the dev console).
    GridStyleCache.CellPx     = ReadCVarFloat(Console, "editor.grid.cell_px", 14.0f);
    GridStyleCache.Opacity    = ReadCVarFloat(Console, "editor.grid.opacity", 0.6f);
    GridStyleCache.Brightness = ReadCVarFloat(Console, "editor.grid.brightness", 0.62f);
    GridStyleCache.FadeStart  = ReadCVarFloat(Console, "editor.grid.fade_start", -0.3f);

    BloomEnabled = ReadCVarBool(Console, "editor.bloom.enable", true);
    BloomParamsCache.Threshold = ReadCVarFloat(Console, "editor.bloom.threshold", 1.0f);
    BloomParamsCache.Intensity = ReadCVarFloat(Console, "editor.bloom.intensity", 1.0f);
    BloomParamsCache.Radius    = ReadCVarFloat(Console, "editor.bloom.radius", 2.0f);

    Targets.BeginFrame(frame.FrameInFlightIndex, frame.Retirement);
    Highlight.BeginFrame();

    // Build the scene draw queues once per frame; the per-viewport camera is applied at
    // draw time, so every viewport reuses the same brush + placed-mesh queues. Brush
    // geometry re-uploads only when the scene's brushes changed (dirty-tracked inside).
    if (MaterialPath)
    {
        // Stamp the live render.* tunables before Build: the shadow-view
        // gather multiplies in ShadowSoftness, so it must be current when the
        // lights are packed. Reset() inside Build preserves these fields.
        RenderLightSet& lights = QueueBuilder->Lights();
        ApplyRendererCVars(Console, lights);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        // The one tunable the shared reader leaves alone: the editor selects it
        // from its view menu rather than from render.debug.view.
        lights.DebugView = WorldView.DebugViewMode;
#endif

        QueueBuilder->Build(World.FocusDocument());

        // Context zones build their own queues so they render real materials.
        std::erase_if(ContextBuilders, [&](const auto& entry)
                      { return !World.IsZoneOpen(ZoneId{ entry.first }); });
        EditorDocument& focusDocument = World.FocusDocument();
        World.VisitOpenZones(
            [&](ZoneId zone, EditorDocument& document, const ZoneViewState& view)
            {
                if (&document == &focusDocument || !view.VisibleInEditor)
                    return;
                auto& builder = ContextBuilders[zone.Value];
                if (builder == nullptr)
                    builder = std::make_unique<SceneRenderQueueBuilder>(
                        RuntimeAssetsRef->Assets, *RuntimeAssetsRef->StaticMeshes,
                        RuntimeAssetsRef->Materials, RuntimeAssetsRef->MaterialSets,
                        *LoggingRef);
                builder->Build(document);
            });
        // Arbitrate and record the focus scene's shadow atlas once per frame,
        // before any viewport rendering scope opens. Every Solid viewport
        // then samples the same tiles.
        UpdateShadowResidency(frame);
    }

    // Render every viewport that is actually on screen. A hidden panel zeroes its
    // viewport's rect, so a degenerate rect means "not shown this frame"; its
    // target is pruned below and rebuilt when the panel reappears.
    std::vector<EditorViewport*> live;
    for (const auto& viewport : Layout.All())
        if (viewport != nullptr
            && viewport->RegionMax.x > viewport->RegionMin.x
            && viewport->RegionMax.y > viewport->RegionMin.y)
            live.push_back(&*viewport);

    std::vector<ViewportId> liveIds;
    liveIds.reserve(live.size());
    for (EditorViewport* viewport : live)
    {
        liveIds.push_back(viewport->Id);
        if (std::optional<ViewportTargetCache::RenderView> target = Targets.AcquireForRender(viewport->Id))
            RenderViewportOffscreen(frame, *viewport, *target);
    }

    // Drop targets for viewports the layout no longer shows.
    Targets.Prune(liveIds);
}

void EditorRenderFeature::UpdateShadowResidency(const FrameContext& frame)
{
    RenderLightSet& lights = QueueBuilder->Lights();

    // A different focus scene means different entity keys; reset instead of
    // aging the old scene's slot holders out through steal hysteresis.
    const RegistryId sceneRegistry = World.FocusDocument().GetScene().GetRegistry().Id;
    if (!(sceneRegistry == ShadowSceneRegistry))
    {
        Residency.Reset();
        ShadowSceneRegistry = sceneRegistry;
    }

    // Depth bias bakes into tiles at record time, so cached tiles keep an old
    // bias until re-rendered; a bias cvar edit invalidates them all.
    if (lights.ShadowBiasConstant != ShadowBiasConstant
        || lights.ShadowBiasSlope != ShadowBiasSlope)
    {
        Residency.InvalidateAll();
        ShadowBiasConstant = lights.ShadowBiasConstant;
        ShadowBiasSlope = lights.ShadowBiasSlope;
    }

    const std::span<const SpotShadowRequest> requests =
        QueueBuilder->BuildShadowRequests(ShadowScoreOrigin());
    const std::span<const PointShadowRequest> pointRequests =
        QueueBuilder->BuildPointShadowRequests(ShadowScoreOrigin());

    // The diff always swaps its tables so a later OnChange acquisition sees
    // current history; events are only worth emitting while someone caches.
    CasterEvents.clear();
    CasterDiff.Apply(QueueBuilder->Casters().Records,
                     Residency.HasOnChangeSlots(), CasterEvents);

    const ShadowResidencyBudgets budgets =
        EngineConsoleBuiltins::ReadShadowResidencyBudgets(Console);
    Residency.Update(requests, pointRequests, CasterEvents, budgets);
    Residency.ApplyGrants(lights);

    ShadowPass.Draw(frame, lights, Residency.ScheduledViews(),
                    Residency.ScheduledPointFaces(),
                    QueueBuilder->Casters(), *MeshCache, &Residency);

    ShadowFrame.Active = Lighting.HasAtlas() || Lighting.HasCubePool();
    ShadowFrame.FocusRegistry = sceneRegistry;
    ShadowFrame.Stats = Residency.FrameStats();
    ShadowFrame.Budgets = budgets;
    for (std::uint32_t slot = 0; slot < kMaxSpotShadows; ++slot)
        ShadowFrame.Slots[slot] = Residency.SlotInfo(slot);
    for (std::uint32_t slot = 0; slot < kMaxPointShadows; ++slot)
        ShadowFrame.PointSlots[slot] = Residency.PointSlotInfo(slot);
    ShadowFrame.Rows.clear();
    ShadowFrame.Rows.reserve(requests.size() + pointRequests.size());
    for (const SpotShadowRequest& request : requests)
    {
        ShadowResidencyReadout::LightRow row;
        row.Entity = request.Key.Entity;
        row.Type = GpuLightType::Spot;
        row.Score = request.Score;
        row.TileSize = request.TileSize;
        row.Policy = request.Policy;
        for (std::uint32_t slot = 0; slot < kMaxSpotShadows; ++slot)
        {
            const SpotShadowSlotInfo& info = ShadowFrame.Slots[slot];
            if (info.Live && info.Owner == request.Key)
            {
                row.Held = true;
                row.Slot = slot;
                break;
            }
        }
        ShadowFrame.Rows.push_back(row);
    }
    for (const PointShadowRequest& request : pointRequests)
    {
        ShadowResidencyReadout::LightRow row;
        row.Entity = request.Key.Entity;
        row.Type = GpuLightType::Point;
        row.Score = request.Score;
        row.TileSize = kPointShadowFaceExtent;
        row.Policy = request.Policy;
        for (std::uint32_t slot = 0; slot < kMaxPointShadows; ++slot)
        {
            const PointShadowSlotInfo& info = ShadowFrame.PointSlots[slot];
            if (info.Live && info.Owner == request.Key)
            {
                row.Held = true;
                row.Slot = slot;
                break;
            }
        }
        ShadowFrame.Rows.push_back(row);
    }
}

Vec<3> EditorRenderFeature::ShadowScoreOrigin() const
{
    const EditorViewport* reference = Layout.Active();
    if (reference == nullptr
        || reference->Orientation != ViewportOrientation::Perspective)
    {
        for (const auto& viewport : Layout.All())
        {
            if (viewport != nullptr
                && viewport->Orientation == ViewportOrientation::Perspective)
            {
                reference = &*viewport;
                break;
            }
        }
    }
    if (reference == nullptr)
        return Vec<3>(0.0f, 0.0f, 0.0f);
    return reference->Camera.Position;
}

void EditorRenderFeature::RenderViewportOffscreen(const FrameContext& frame, EditorViewport& viewport,
                                                  const ViewportTargetCache::RenderView& target)
{
    // The scene renderers derive their Vulkan viewport/scissor from the viewport's
    // screen rect; the offscreen target is origin-(0,0) and sized to the region, so
    // override the rect for this pass (same size, so the camera aspect is unchanged)
    // and restore it after. Picking reads the screen rect on the input path, not here.
    const ImVec2 savedMin = viewport.RegionMin;
    const ImVec2 savedMax = viewport.RegionMax;
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(static_cast<float>(target.Extent.width),
                                static_cast<float>(target.Extent.height));

    const auto transitionColor = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                                     VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target.ColorImage;
        t.OldLayout = oldLayout;
        t.NewLayout = newLayout;
        t.SrcStage = srcStage;
        t.DstStage = dstStage;
        t.SrcAccess = srcAccess;
        t.DstAccess = dstAccess;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    };

    // Color: whatever it held (UNDEFINED on first use, else SHADER_READ from when it
    // was last sampled) -> COLOR_ATTACHMENT.
    transitionColor(*target.ColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Depth is cleared and discarded each pass, so its prior contents never matter.
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target.DepthImage;
        t.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        t.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    }

    // The offscreen target is RGBA16F linear; the scene pipelines key on these
    // formats and rebuild their RGBA16F variant transparently.
    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = target.Extent;
    scope.Color.View = target.ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };
    scope.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    scope.Depth.View = target.DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    scope.DepthFormat = Services.DepthFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);
        const FrameContext& local = rendering.Context();

        EditorDocument& focusDocument = World.FocusDocument();
        const EditorScene& scene = focusDocument.GetScene();

        // A perspective viewport shows the game's background; an ortho one keeps
        // the flat editor backdrop, since a sky in a 2D working view describes
        // nothing. Either way exactly one of the two fills the target.
        const RenderLightSet& viewLights = QueueBuilder->Lights();
        if (viewport.Orientation == ViewportOrientation::Perspective && viewLights.SkyEnabled)
        {
            const CameraRenderData viewCamera = viewport.BuildRenderData();
            Sky.Draw(local,
                     MakeInverseSkyViewProjection(viewCamera.View, viewCamera.Projection),
                     SkyGradientParams{ .Top = viewLights.AmbientSky,
                                        .Bottom = viewLights.AmbientGround });
        }
        else
        {
            Backdrop.DrawViewport(local.Cmd, viewport, local.TargetExtent, local.TargetFormat, local.DepthFormat);
        }
        Grid.DrawViewport(local.Cmd, viewport, GridCfg, GridStyleCache, local.TargetExtent, local.TargetFormat, local.DepthFormat);

        // Context zones (open, visible, not the focus) render dimmed and reduced:
        // solid-preview or wireframe body plus component visuals, modulated by the
        // theme dim constant. No real-material pass, no selection highlight, no
        // hover glow; picking never sees them, so they read as present but inert.
        World.VisitOpenZones(
            [&](ZoneId zone, EditorDocument& document, const ZoneViewState& view)
            {
                if (&document == &focusDocument || !view.VisibleInEditor)
                    return;
                const EditorScene& contextScene = document.GetScene();
                if (viewport.Shading == ViewportShading::Solid)
                {
                    // Real materials under the grey overlay when the WYSIWYG path
                    // is up; the checker fallback only without an asset system.
                    const auto it = ContextBuilders.find(zone.Value);
                    if (MaterialPath && it != ContextBuilders.end())
                    {
                        // Full-bright neutral ambient under a translucent grey wash:
                        // the textures stay readable and the grey does not depend on
                        // whether a focus-zone light happens to reach this zone.
                        RenderLightSet contextLights;
                        contextLights.AmbientSky = Vec<3>(1.0f, 1.0f, 1.0f);
                        contextLights.AmbientGround = Vec<3>(1.0f, 1.0f, 1.0f);
    #ifdef SENCHA_ENABLE_RENDER_PROFILING
                        contextLights.DebugView = WorldView.DebugViewMode;
    #endif
                        Forward.Draw(local, viewport.BuildRenderData(), contextLights,
                                     it->second->BrushQueue(), *MeshCache, *MaterialStore);
                        // Placed meshes cannot receive the brush-triangle wash, so
                        // the overlay folds into their multiply tint instead (exact
                        // on white, close on bright textures).
                        const Vec4& wash = EditorTheme::ContextZoneOverlay;
                        const Vec4 meshDim(1.0f - wash.W + wash.X * wash.W,
                                           1.0f - wash.W + wash.Y * wash.W,
                                           1.0f - wash.W + wash.Z * wash.W, 1.0f);
                        Forward.Draw(local, viewport.BuildRenderData(), contextLights,
                                     it->second->MeshQueue(), *MeshCache, *MaterialStore,
                                     meshDim);
                        BrushFills.DrawZoneOverlay(local, viewport, contextScene,
                                                   EditorTheme::ContextZoneOverlay);
                    }
                    else
                    {
                        BrushSolid.DrawViewportTinted(local, viewport, contextScene,
                                                      EditorTheme::ContextZoneDim);
                    }
                }
                else
                {
                    const Vec4 dimmedWire(EditorTheme::ContextZoneDim.X, 0.0f, 0.0f, 1.0f);
                    Wireframe.DrawWireframe(local, viewport, contextScene, dimmedWire);
                }
                Visuals.DrawViewport(local, viewport, contextScene, EditorTheme::ContextZoneDim);
            });

        // The focus zone renders exactly as a single document does.
        if (IBrushBodyRenderer* body = BodyRenderers[static_cast<std::size_t>(viewport.Shading)])
            body->DrawViewport(local, viewport, scene);
        // Placed meshes draw in every viewport so they read regardless of shading: through
        // the real-material queue when active, else the procedural-checker fallback.
        if (MaterialPath)
            Forward.Draw(local, viewport.BuildRenderData(), QueueBuilder->Lights(),
                         QueueBuilder->MeshQueue(), *MeshCache, *MaterialStore);
        else
            Meshes.DrawViewport(local, viewport, scene);
        Visuals.DrawViewport(local, viewport, scene, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        IrradianceVolumes.DrawViewport(local, viewport, scene);
        Highlight.DrawViewport(local, viewport, scene, *Session());
        if (WorldView.ShowZoneBounds || WorldView.StreamingPreview)
            ZoneBounds.DrawViewport(local, viewport, World, WorldView);
        Affordances.DrawViewport(local, viewport);
        Preview.DrawViewport(local, viewport);
    }

    // Color: COLOR_ATTACHMENT -> SHADER_READ_ONLY for the UI's ImGui::Image sample.
    transitionColor(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    *target.ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (BloomEnabled)
        RecordViewportBloom(frame, viewport, target);

    viewport.RegionMin = savedMin;
    viewport.RegionMax = savedMax;
}

void EditorRenderFeature::RecordViewportBloom(const FrameContext& frame, EditorViewport& viewport,
                                              const ViewportTargetCache::RenderView& target)
{
    if (target.BloomImage[0] == VK_NULL_HANDLE)
        return;

    // Render the active wireframe ON TOP (no depth test) into the bloom source, so the
    // glow comes from the full line instead of the depth-tested scene copy (which clips
    // itself against its own near surface). The crisp visible wireframe still depth-tests
    // in the scene pass; this is purely the glow's source. The viewport depth is attached
    // but untested (the wide-line pipeline expects a depth format). Then bloom it onto the
    // scene; the color ends in SHADER_READ_ONLY for the UI composite.
    VulkanBarriers::ImageTransition toColor{};
    toColor.Image = target.BloomImage[0];
    toColor.OldLayout = *target.BloomLayout[0];
    toColor.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.SrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toColor.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.SrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toColor.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toColor);

    const ImVec2 savedMin = viewport.RegionMin;
    const ImVec2 savedMax = viewport.RegionMax;
    viewport.RegionMin = ImVec2(0.0f, 0.0f);
    viewport.RegionMax = ImVec2(static_cast<float>(target.BloomExtent.width),
                                static_cast<float>(target.BloomExtent.height));

    // Depth is attached but neither loaded nor stored: the wide-line pipeline
    // expects a depth format, and this pass does not test against it.
    RenderScopeDesc glowScope{};
    glowScope.Area.extent = target.BloomExtent;
    glowScope.Color.View = target.BloomView[0];
    glowScope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    glowScope.Color.Clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    glowScope.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    glowScope.Depth.View = target.DepthView;
    glowScope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    glowScope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    glowScope.Depth.Clear.depthStencil = { 1.0f, 0 };
    glowScope.DepthFormat = Services.DepthFormat;
    glowScope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope glowRendering(frame, glowScope);
        Highlight.SubmitActiveGlowSource(glowRendering.Context(), viewport,
                                         World.FocusDocument().GetScene());
    }

    VulkanBarriers::ImageTransition toRead{};
    toRead.Image = target.BloomImage[0];
    toRead.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toRead.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toRead.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toRead);
    *target.BloomLayout[0] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    Bloom.Record(frame, target, BloomParamsCache);

    viewport.RegionMin = savedMin;
    viewport.RegionMax = savedMax;
}

void EditorRenderFeature::ReleaseSceneResources()
{
    // Point the Solid body back at the checker so nothing dereferences the released
    // builder, then drop the brush GPU meshes + material refs while the caches live.
    BodyRenderers[static_cast<std::size_t>(ViewportShading::Solid)] = &BrushSolid;
    MaterialPath = false;
    SceneSolid.reset();
    QueueBuilder.reset();
    MeshCache = nullptr;
    MaterialStore = nullptr;
}

void EditorRenderFeature::Teardown()
{
    Backdrop.Teardown();
    Sky.Teardown();
    Grid.Teardown();
    Solid.Teardown();
    Forward.Teardown();
    ShadowPass.Teardown();
    Lighting.Teardown();
    Lines.Teardown();
    WideLines.Teardown();
    Fills.Teardown();
    Targets.Teardown();
    Bloom.Teardown();
}
