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
#include <render/ProjectedShadowCVars.h>
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
        SkinnedMeshCacheRef = runtimeAssets->SkinnedMeshes.get();
        MaterialStore = &runtimeAssets->Materials;
        QueueBuilder.emplace(runtimeAssets->Assets, *runtimeAssets->StaticMeshes,
                             runtimeAssets->Materials, runtimeAssets->MaterialSets,
                             logging, runtimeAssets->Textures.get(),
                             runtimeAssets->SkinnedMeshes.get());
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
    // After ShadowPass, which creates the set-2 layout this reads. The frame-UBO
    // range no longer depends on the order: each pass declares the block it
    // needs and the descriptor cache keeps the largest.
    Forward.Setup(services, Lighting);
    Lines.Setup(services);
    WideLines.Setup(services);
    Fills.Setup(services);
    Targets.Setup(services);
    Bloom.Setup(services);
    ProjectedSilhouettes.Setup(services, sizeof(StaticMeshVertex));
    ProjectedProject.Setup(services);
    Composition.Setup(services.Logging);
    ShadowAtlasReady = Composition.DeclarePoint("ShadowAtlasReady");
    ProjectedSilhouettesReady = Composition.DeclarePoint("ProjectedSilhouettesReady");
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
    Composition.Clear();

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
                        *LoggingRef, nullptr,
                        RuntimeAssetsRef->SkinnedMeshes.get());
                builder->Build(document);
            });
        // Arbitrating and recording the focus scene's shadow atlas is one
        // frame's work that every Solid viewport then samples, so it is
        // declared as work the views wait on rather than called first and
        // documented as such.
        Composition.AddWork({
            .Name = "shadow_residency",
            .Record = { [](void* self, const FrameContext& context)
                        { static_cast<EditorRenderFeature*>(self)->UpdateShadowResidency(context); },
                        this },
            .Produces = ShadowAtlasReady,
        });
        // Always produced while the material path is up, even with zero
        // casters: the views' dependency list is static, and a point nobody
        // produced would skip every view.
        Composition.AddWork({
            .Name = "projected_silhouettes",
            .Record = { [](void* self, const FrameContext& context)
                        { static_cast<EditorRenderFeature*>(self)->RecordProjectedSilhouettes(context); },
                        this },
            .Produces = ProjectedSilhouettesReady,
        });
    }

    // Every viewport that is actually on screen. A hidden panel zeroes its
    // viewport's rect, so a degenerate rect means "not shown this frame"; its
    // target is pruned below and rebuilt when the panel reappears.
    ViewSlots.clear();
    LiveViewports.clear();
    for (const auto& viewport : Layout.All())
    {
        if (viewport == nullptr
            || viewport->RegionMax.x <= viewport->RegionMin.x
            || viewport->RegionMax.y <= viewport->RegionMin.y)
            continue;
        // Pruning is about which panels the layout still shows, so it counts a
        // viewport whose target is not renderable yet -- a panel in its first
        // frame has not reported a size.
        LiveViewports.push_back(viewport->Id);
        if (std::optional<ViewportTargetCache::RenderView> target =
                Targets.AcquireForRender(viewport->Id))
            ViewSlots.push_back({ .Viewport = &*viewport, .Target = *target });
    }

    // Without the material path there is no shadow work to wait on, and a view
    // that waited on a point nobody produced would be skipped outright.
    const DependencyPointId shadowDependency[] = { ShadowAtlasReady,
                                                   ProjectedSilhouettesReady };
    const std::span<const DependencyPointId> viewDependsOn =
        MaterialPath ? std::span<const DependencyPointId>(shadowDependency)
                     : std::span<const DependencyPointId>{};

    // The shared mask target's extent for this frame: the largest live
    // viewport, so every view's DrawMask sees the same extent and the store
    // never rebuilds mid-frame.
    MaskExtent = {};
    for (const ViewSlot& slot : ViewSlots)
    {
        MaskExtent.width = std::max(MaskExtent.width, slot.Target.Extent.width);
        MaskExtent.height = std::max(MaskExtent.height, slot.Target.Extent.height);
    }

    // Separate loop on purpose: a declared view holds a pointer into ViewSlots,
    // and pushing to it above would move the ones already declared.
    for (ViewSlot& slot : ViewSlots)
        Composition.AddView({
            .View = { .Name = "viewport",
                      .Target = slot.Target.Scene,
                      // Built once here rather than by each renderer inside the
                      // view: same camera, same frame, one construction.
                      .Camera = slot.Viewport->CameraForExtent(slot.Target.Extent.width,
                                                              slot.Target.Extent.height),
                      .User = &slot },
            .Record = { [](void* self, const FrameContext& context, const FrameView& view)
                        { static_cast<EditorRenderFeature*>(self)->RecordViewportView(context, view); },
                        this },
            .DependsOn = viewDependsOn,
        });

    Composition.Execute(frame);

    // Drop targets for viewports the layout no longer shows.
    Targets.Prune(LiveViewports);
}

void EditorRenderFeature::RecordProjectedSilhouettes(const FrameContext& frame)
{
    ProjectedFrame.Reset();
    ProjectedSweptBounds.clear();
    if (QueueBuilder == std::nullopt || SkinnedMeshCacheRef == nullptr)
        return;

    const RenderLightSet& lights = QueueBuilder->Lights();
    ProjectedShadowSet& casters = QueueBuilder->ProjectedCasters();
    if (!lights.ProjectedShadowsEnabled || casters.Casters.empty())
        return;

    ProjectedBudgets = ReadProjectedShadowBudgets(Console);

    // Directions smooth against the UI clock; the editor has no simulation
    // tick, and what the smoothing needs is only a monotonic dt.
    ProjectedShadowDirectionParams params;
    params.FallbackDirection = lights.ProjectedShadowFallbackDirection;
    params.SmoothingRate = lights.ProjectedShadowSmoothing;
    params.MinPitchDegrees = lights.ProjectedShadowMinPitchDegrees;
    UpdateProjectedShadowDirections(
        casters, std::span<const GpuLight>(lights.Lights, lights.Count),
        ProjectedDirections, std::min(ImGui::GetIO().DeltaTime, 0.1f), params);

    RankAndClampProjectedCasters(casters, ShadowScoreOrigin(),
                                 ProjectedBudgets.MaxCasters);
    const ProjectedShadowTileGrid grid = MakeProjectedShadowTileGrid(
        ProjectedBudgets.MaxCasters, ProjectedBudgets.TilePixels);

    // One pass per caster builds the silhouette draw and its projection
    // record together, so a skipped caster (unresident mesh, masked-out
    // sections, no receivers) can never skew a later caster's tile or
    // view-projection: the tile ordinal IS the position in casterDraws,
    // which is the ordinal the silhouette pass renders. The atlas bindless
    // index is not known until that pass has run, so it is stamped into the
    // collected uniforms afterwards.
    //
    // Receivers come from both WYSIWYG queues -- brush cells and placed
    // meshes both live in the shared static cache.
    std::vector<ProjectedSilhouetteCasterDraw> casterDraws;
    std::vector<ProjectedSilhouetteSectionDraw> sectionDraws;
    ProjectedFrame.VertexStride = sizeof(StaticMeshVertex);
    for (const ProjectedShadowCaster& caster : casters.Casters)
    {
        const GpuStaticMesh* mesh = SkinnedMeshCacheRef->Get(caster.Mesh);
        if (mesh == nullptr)
            continue;
        const Mat4 viewProjection =
            MakeProjectedShadowViewProjection(caster, ProjectedBudgets.MaxDistance);
        ProjectedSilhouetteCasterDraw draw;
        draw.Mvp = viewProjection * caster.WorldMatrix;
        draw.FirstSection = static_cast<std::uint32_t>(sectionDraws.size());
        for (std::uint32_t section = 0; section < mesh->Sections.size(); ++section)
        {
            if ((caster.SectionMask & (1u << section)) == 0)
                continue;
            sectionDraws.push_back(ProjectedSilhouetteSectionDraw{
                .Vertex = mesh->VertexBuffer,
                .Index = mesh->IndexBuffer,
                .IndexCount = mesh->Sections[section].IndexCount,
                .IndexOffset = mesh->Sections[section].IndexOffset,
            });
        }
        draw.SectionCount =
            static_cast<std::uint32_t>(sectionDraws.size()) - draw.FirstSection;
        if (draw.SectionCount == 0)
            continue;
        const std::uint32_t thisTile =
            static_cast<std::uint32_t>(casterDraws.size());
        casterDraws.push_back(draw);

        const Aabb3d swept =
            ProjectedShadowSweptBounds(caster, ProjectedBudgets.MaxDistance);

        ProjectedShadowProjection projection;
        projection.Uniform.ShadowViewProjection = viewProjection;
        projection.Uniform.TileScaleBias =
            ProjectedShadowTileUvScaleBias(grid, thisTile);
        projection.Uniform.Params = Vec4(lights.ProjectedShadowDarkness,
                                         lights.ProjectedShadowFadeStart,
                                         0.0f, 0.0f);
        projection.FirstReceiver =
            static_cast<std::uint32_t>(ProjectedFrame.Receivers.size());

        const auto appendReceivers = [&](const RenderQueue& queue)
        {
            ProjectedReceiverScratch.clear();
            GatherProjectedShadowReceivers(queue.Opaque(), swept,
                                           ProjectedBudgets.MaxReceiversPerCaster,
                                           ProjectedReceiverScratch);
            for (const std::uint32_t itemIndex : ProjectedReceiverScratch)
            {
                const RenderQueueItem& item = queue.Opaque()[itemIndex];
                const GpuStaticMesh* receiverMesh = MeshCache->Get(item.Mesh);
                if (receiverMesh == nullptr
                    || item.SectionIndex >= receiverMesh->Sections.size())
                    continue;
                const StaticMeshSection& section =
                    receiverMesh->Sections[item.SectionIndex];
                ProjectedFrame.Receivers.push_back(ProjectedReceiverDraw{
                    .Vertex = receiverMesh->VertexBuffer,
                    .Index = receiverMesh->IndexBuffer,
                    .IndexCount = section.IndexCount,
                    .IndexOffset = section.IndexOffset,
                    .World = item.WorldMatrix,
                });
            }
        };
        appendReceivers(QueueBuilder->BrushQueue());
        appendReceivers(QueueBuilder->MeshQueue());

        projection.ReceiverCount =
            static_cast<std::uint32_t>(ProjectedFrame.Receivers.size())
            - projection.FirstReceiver;
        if (projection.ReceiverCount == 0)
            continue;
        ProjectedFrame.Casters.push_back(projection);
        ProjectedSweptBounds.push_back(swept);
    }
    if (casterDraws.empty())
    {
        ProjectedFrame.Reset();
        ProjectedSweptBounds.clear();
        return;
    }

    ProjectedSilhouetteInput silhouettes;
    silhouettes.TilesPerRow = grid.TilesPerRow;
    silhouettes.TilePixels = grid.TilePixels;
    silhouettes.SoftnessTexels = ProjectedBudgets.SoftnessTexels;
    silhouettes.Casters = casterDraws;
    silhouettes.Sections = sectionDraws;
    if (!ProjectedSilhouettes.Draw(frame, silhouettes))
    {
        // The projections sample an atlas that never rendered this frame.
        ProjectedFrame.Reset();
        ProjectedSweptBounds.clear();
        return;
    }
    const float atlasIndex =
        static_cast<float>(ProjectedSilhouettes.AtlasBindlessIndex());
    for (ProjectedShadowProjection& projection : ProjectedFrame.Casters)
        projection.Uniform.Params.Z = atlasIndex;

    ProjectedFrame.Ready = !ProjectedFrame.Casters.empty();
}

void EditorRenderFeature::RecordViewportView(const FrameContext& frame, const FrameView& view)
{
    ViewSlot& slot = *static_cast<ViewSlot*>(view.User);
    RenderViewportOffscreen(frame, *slot.Viewport, slot.Target, view.Camera);
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
        ProjectedDirections.clear();
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
                                                  const ViewportTargetCache::RenderView& target,
                                                  const CameraRenderData& camera)
{
    // The scene renderers derive their Vulkan viewport/scissor -- and, until they
    // are handed the camera above, their own aspect ratio -- from the viewport's
    // screen rect. The offscreen target is origin-(0,0) and sized to the target,
    // so override the rect for this pass and restore it after. Picking reads the
    // screen rect on the input path, not here.
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
    const bool projectHere = ProjectedFrame.Ready && MaterialPath
        && viewport.Orientation == ViewportOrientation::Perspective
        && viewport.Shading == ViewportShading::Solid;
    scope.Depth.View = target.DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // A projecting view suspends this instance mid-frame to write the shadow
    // mask against the opaque depth; the contents must survive that boundary.
    // Every other view keeps the discard.
    scope.Depth.StoreOp = projectHere ? VK_ATTACHMENT_STORE_OP_STORE
                                      : VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
            Sky.Draw(local,
                     MakeInverseSkyViewProjection(camera.View, camera.Projection),
                     SkyGradientParams{ .Top = viewLights.AmbientSky,
                                        .Bottom = viewLights.AmbientGround,
                                        .Exposure = viewLights.Exposure,
                                        .TonemapKnee = viewLights.TonemapKnee,
                                        .TonemapEnabled = viewLights.TonemapEnabled });
        }
        else
        {
            Backdrop.DrawViewport(local.Cmd, viewport, local.TargetExtent, local.TargetFormat, local.DepthFormat);
        }
        Grid.DrawViewport(local.Cmd, viewport, camera, GridCfg, GridStyleCache, local.TargetExtent, local.TargetFormat, local.DepthFormat);

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
                        Forward.Draw(local, camera, contextLights,
                                     it->second->BrushQueue(), *MeshCache, *MaterialStore,
                                     SkinnedMeshCacheRef);
                        // Placed meshes cannot receive the brush-triangle wash, so
                        // the overlay folds into their multiply tint instead (exact
                        // on white, close on bright textures).
                        const Vec4& wash = EditorTheme::ContextZoneOverlay;
                        const Vec4 meshDim(1.0f - wash.W + wash.X * wash.W,
                                           1.0f - wash.W + wash.Y * wash.W,
                                           1.0f - wash.W + wash.Z * wash.W, 1.0f);
                        Forward.Draw(local, camera, contextLights,
                                     it->second->MeshQueue(), *MeshCache, *MaterialStore,
                                     SkinnedMeshCacheRef, meshDim);
                        BrushFills.DrawZoneOverlay(local, viewport, camera, contextScene,
                                                   EditorTheme::ContextZoneOverlay);
                    }
                    else
                    {
                        BrushSolid.DrawViewportTinted(local, viewport, camera, contextScene,
                                                      EditorTheme::ContextZoneDim);
                    }
                }
                else
                {
                    const Vec4 dimmedWire(EditorTheme::ContextZoneDim.X, 0.0f, 0.0f, 1.0f);
                    Wireframe.DrawWireframe(local, viewport, camera, contextScene, dimmedWire);
                }
                Visuals.DrawViewport(local, viewport, camera, contextScene, EditorTheme::ContextZoneDim);
            });

        // The focus zone renders exactly as a single document does.
        if (IBrushBodyRenderer* body = BodyRenderers[static_cast<std::size_t>(viewport.Shading)])
            body->DrawViewport(local, viewport, camera, scene);
        // Placed meshes draw in every viewport so they read regardless of shading: through
        // the real-material queue when active, else the procedural-checker fallback.
        // Perspective Solid viewports apply the grounding projections between the
        // opaque and transparent halves, the same ordering the game uses; ortho
        // working views skip them the way they skip the sky -- a blob under a
        // character describes nothing in a 2D view. Brush transparents drew with
        // the body above, so a shadow can land on them: recorded limit of the
        // editor's two-queue split, not of the mechanism.
        if (MaterialPath)
        {
            const MeshForwardPass::DrawToken token = Forward.DrawOpaque(
                local, camera, QueueBuilder->Lights(),
                QueueBuilder->MeshQueue(), *MeshCache, *MaterialStore,
                SkinnedMeshCacheRef);
            if (projectHere)
            {
                UnionScratch.clear();
                for (std::size_t i = 0; i < ProjectedFrame.Casters.size(); ++i)
                {
                    ProjectedShadowProjection& projection = ProjectedFrame.Casters[i];
                    projection.Uniform.CameraViewProjection = camera.ViewProjection;
                    const ProjectedShadowScreenRect rect =
                        ComputeProjectedShadowScreenRect(
                            ProjectedSweptBounds[i], camera.ViewProjection,
                            local.TargetExtent.width, local.TargetExtent.height);
                    projection.ScissorX = rect.X;
                    projection.ScissorY = rect.Y;
                    projection.ScissorWidth = rect.Width;
                    projection.ScissorHeight = rect.Height;
                    UnionScratch.push_back(rect);
                }
                ProjectedShadowProjectionInput input;
                input.VertexStride = ProjectedFrame.VertexStride;
                input.Casters = ProjectedFrame.Casters;
                input.Receivers = ProjectedFrame.Receivers;

                // The mask needs this view's opaque depth in its own scope:
                // suspend the viewport's instance, write the mask, resume,
                // composite once. One shared mask target serves every view
                // in turn -- views record sequentially, and each renders at
                // the origin, so the composite maps UVs by scale alone.
                const ProjectedShadowScreenRect unionRect =
                    UnionProjectedShadowScreenRects(UnionScratch);
                RenderScopeInterruption gap(local);
                const bool masked =
                    ProjectedProject.DrawMask(local, input, MaskExtent);
                gap.Resume();
                if (masked)
                    ProjectedProject.Composite(
                        local, QueueBuilder->Lights().ProjectedShadowDarkness,
                        unionRect.X, unionRect.Y,
                        unionRect.Width, unionRect.Height);
            }
            Forward.DrawTransparent(local, QueueBuilder->MeshQueue(), *MeshCache,
                                    *MaterialStore, SkinnedMeshCacheRef,
                                    Vec4(1.0f, 1.0f, 1.0f, 1.0f), token);
        }
        else
            Meshes.DrawViewport(local, viewport, camera, scene);
        Visuals.DrawViewport(local, viewport, camera, scene, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        IrradianceVolumes.DrawViewport(local, viewport, camera, scene);
        Highlight.DrawViewport(local, viewport, camera, scene, *Session());
        if (WorldView.ShowZoneBounds || WorldView.StreamingPreview)
            ZoneBounds.DrawViewport(local, viewport, camera, World, WorldView);
        Affordances.DrawViewport(local, viewport, camera);
        Preview.DrawViewport(local, viewport, camera);
    }

    // Color: COLOR_ATTACHMENT -> SHADER_READ_ONLY for the UI's ImGui::Image sample.
    transitionColor(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    *target.ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (BloomEnabled)
        RecordViewportBloom(frame, viewport, target, camera);

    viewport.RegionMin = savedMin;
    viewport.RegionMax = savedMax;
}

void EditorRenderFeature::RecordViewportBloom(const FrameContext& frame, EditorViewport& viewport,
                                              const ViewportTargetCache::RenderView& target,
                                              const CameraRenderData& camera)
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
        Highlight.SubmitActiveGlowSource(glowRendering.Context(), viewport, camera,
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
    SkinnedMeshCacheRef = nullptr;
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
    ProjectedSilhouettes.Teardown();
    ProjectedProject.Teardown();
}
