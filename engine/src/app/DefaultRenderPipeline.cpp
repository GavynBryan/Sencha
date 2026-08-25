#include <app/DefaultRenderPipeline.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>
#include <render/RenderLightCVars.h>
#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderStats.h>
#include <world/transform/TransformComponents.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/feature/MeshRenderFeature.h>
#include <render/feature/ShadowRenderFeature.h>
#include <render/feature/SkinnedPoseRenderFeature.h>
#include <render/feature/SkyRenderFeature.h>
#endif

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    RenderDebugView ReadDebugViewCVar(const ConsoleRegistry* console)
    {
        RenderDebugView view = RenderDebugView::None;
        (void)ParseRenderDebugView(
            ReadCVarString(console, "render.debug.view", ""), view);
        return view;
    }
#endif

    void ApplyRendererTunables(const ConsoleRegistry* console, RenderLightSet& lights)
    {
        ApplyRendererCVars(console, lights);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        // Not part of the shared reader: the editor drives this from its own
        // view menu, so the cvar is only the game's source for it.
        lights.DebugView = ReadDebugViewCVar(console);
#endif
    }
}

DefaultRenderPipeline::DefaultRenderPipeline(LoggingProvider* logging,
                                             const ConsoleRegistry* console)
    : Log(logging ? &logging->GetLogger<DefaultRenderPipeline>() : nullptr)
    , Logging(logging)
    , Console(console)
{
}

void DefaultRenderPipeline::SetAssetStores(StaticMeshCache& meshes,
                                           MaterialCache& materials,
                                           MaterialSetCache& materialSets,
                                           TextureCache* textures,
                                           const SkinnedMeshCache* skinnedMeshes,
                                           const AnimationClipCache* clips,
                                           const SkeletonCache* skeletons)
{
    Meshes = &meshes;
    SkinnedMeshes = skinnedMeshes;
    AnimationClips = clips;
    Skeletons = skeletons;
    Materials = &materials;
    MaterialSets = &materialSets;
    Textures = textures;
}

bool DefaultRenderPipeline::AddMeshRenderFeature(GraphicsServices& graphics)
{
#ifdef SENCHA_ENABLE_VULKAN
    if (Meshes == nullptr || Materials == nullptr)
        return false;

    Swapchain = &graphics.Swapchain;

    // Declared edges, not call order. Each one is a real constraint that used
    // to live in a comment above the call that happened to satisfy it:
    //
    //   shadow -> pose   posed skinned geometry exists before anything that
    //                    will draw it as a caster.
    //   mesh   -> shadow the shadow feature's Setup creates the lighting set
    //                    layout the forward pass builds its pipeline layout
    //                    against, and Offscreen records the atlas before
    //                    MainColor samples it.
    //   mesh   -> sky    within MainColor the background fills the view with
    //                    no depth test, so it records first.
    //
    // Staging order below is deliberately not the resolved order: the resolver
    // produces the order, and the goldens are what prove it.
    static constexpr std::string_view kPose = "runtime_skinned_pose";
    static constexpr std::string_view kShadow = "runtime_shadow";
    static constexpr std::string_view kSky = "runtime_sky";
    static constexpr std::array<std::string_view, 1> kShadowDeps{ kPose };
    static constexpr std::array<std::string_view, 2> kMeshDeps{ kShadow, kSky };

    auto bindings = std::make_shared<LightBindings>();
    // Constructed before the mesh feature is staged, which captures it: staging
    // order is free, but the values a feature is constructed with are not.
    if (SkinnedMeshes != nullptr)
        SkinnedPoses = std::make_shared<SkinnedPoseFrameData>();

    graphics.MainRenderer.StageFeature(
        std::make_unique<MeshRenderFeature>(
            Queue, *Meshes, *Materials, Camera, Lights, bindings,
            SkinnedMeshes, SkinnedPoses),
        FeatureRegistration{ .Id = "runtime_mesh", .DependsOn = kMeshDeps });

    graphics.MainRenderer.StageFeature(
        std::make_unique<SkyRenderFeature>(Camera, Lights),
        FeatureRegistration{ .Id = kSky });

    graphics.MainRenderer.StageFeature(
        std::make_unique<ShadowRenderFeature>(
            bindings, Lights, ShadowCasters, *Meshes, Residency),
        FeatureRegistration{ .Id = kShadow,
                             .DependsOn = SkinnedMeshes != nullptr
                                 ? std::span<const std::string_view>(kShadowDeps)
                                 : std::span<const std::string_view>{} });

    if (SkinnedMeshes != nullptr)
    {
        graphics.MainRenderer.StageFeature(
            std::make_unique<SkinnedPoseRenderFeature>(SkinnedPoses, *SkinnedMeshes),
            FeatureRegistration{ .Id = kPose });
    }

    if (!graphics.MainRenderer.CommitStagedFeatures())
        return false;

    // Probe residency shares the lighting set: it swaps binding-2 slots as
    // zones stream and hands headers to extraction. Uploads only ever happen
    // after the shadow feature's Setup has created the set.
    ProbeVolumes.Setup(GpuImages{&graphics.Images}, bindings, Logging);
    return true;
#else
    (void)graphics;
    return false;
#endif
}

void DefaultRenderPipeline::PublishExtractionStats(
    RenderStats& stats, const LightExtractionCounts& lightCounts) const
{
    stats.LightsVisible = lightCounts.Packed;
    stats.LightsDroppedAtCap = lightCounts.DroppedAtCap();
    stats.ShadowCastingLights = static_cast<std::uint32_t>(
        ShadowRequests.size() + PointShadowRequests.size());
    stats.CasterDiffEvents = static_cast<std::uint32_t>(CasterEvents.size());

    stats.ProbeVolumesResident =
        static_cast<std::uint32_t>(ProbeVolumes.ResidentVolumeCount());

    const ShadowFrameStats& shadow = Residency.FrameStats();
    stats.ShadowSlotsHeld = shadow.Spot.HeldRequests + shadow.Point.HeldRequests;
    stats.ShadowCacheHits = shadow.Spot.CachedSlots + shadow.Point.CachedSlots;
    stats.ShadowRequestsDenied =
        shadow.Spot.DeniedRequests + shadow.Point.DeniedRequests;
    stats.PointShadowCubesHeld = shadow.Point.HeldRequests;

    for (std::uint32_t slot = 0; slot < kMaxSpotShadows; ++slot)
    {
        const SpotShadowSlotInfo info = Residency.SlotInfo(slot);
        if (!info.Live || !info.Allocation.IsValid())
            continue;
        // D16 tiles: two bytes per texel.
        stats.ShadowTileBytes += static_cast<std::uint64_t>(info.Allocation.Size)
                               * info.Allocation.Size * 2u;
        if (info.Allocation.Size >= 1024)
            ++stats.AtlasTiles1024;
        else if (info.Allocation.Size >= 512)
            ++stats.AtlasTiles512;
        else
            ++stats.AtlasTiles256;
    }
}

void DefaultRenderPipeline::PublishEmptyFrameOutputs()
{
    Queue.Reset();
    if (SkinnedPoses != nullptr)
        SkinnedPoses->Reset();
    Lights.Reset();
    // The sky is the one feature with no data dependency to fall empty: it
    // draws from the ambient hemisphere whatever the queue holds. Clearing the
    // flag is what makes it skip a frame with no camera, rather than paint the
    // background through a stale view matrix.
    Lights.SkyEnabled = false;
    ShadowRequests.clear();
    PointShadowRequests.clear();
    CasterEvents.clear();
    Residency.ClearFrameSchedule();
}

void DefaultRenderPipeline::ExtractRender(RenderExtractContext& ctx)
{
    PublishEmptyFrameOutputs();

    if (Meshes == nullptr || Materials == nullptr || MaterialSets == nullptr)
        return;

#ifdef SENCHA_ENABLE_VULKAN
    if (Swapchain == nullptr)
        return;
    const VkExtent2D swapchainExtent = Swapchain->GetExtent();
    const RenderExtent extent{ swapchainExtent.width, swapchainExtent.height };
#else
    (void)ctx;
    return;
#endif

    World& world = ctx.Entities;
    const ActiveCameraService* activeCamera =
        world.TryGetResource<ActiveCameraService>();
    if (activeCamera == nullptr || !activeCamera->HasActive())
        return;

    const EntityId cameraEntity = activeCamera->GetActive();
    if (!world.IsAlive(cameraEntity)
        || !ctx.Partitions.Contains(world.GetEntityPartition(cameraEntity))
        || !CameraRenderDataSystem::Build(*activeCamera, world, extent, Camera))
    {
        return;
    }

    CpuScopeTimings* scopes =
        Instrumentation != nullptr ? Instrumentation->CpuScopes : nullptr;

    {
        CpuScopeTimer timer(scopes, CpuScope::Extraction);
        RenderExtractor.Extract(
            world, ctx.Partitions,
            RenderExtractCaches{ *Meshes, *Materials, *MaterialSets, Textures,
                                 SkinnedMeshes, AnimationClips, Skeletons },
            Camera, Queue, ctx.Presentation.Alpha, SkinnedPoses.get());
        Queue.SortOpaque();
    }

    LightExtractionCounts lightCounts;
    {
        CpuScopeTimer timer(scopes, CpuScope::LightSelection);
        // Back on by default before the tunables read, which takes the current
        // value as its default: leaving the empty frame's cleared flag in place
        // would latch the sky off for the rest of the session whenever the cvar
        // is unset.
        Lights.SkyEnabled = true;
        ApplyRendererTunables(Console, Lights);
        LightExtractor.Extract(world, ctx.Partitions, Camera, Lights,
                               ShadowRequests, PointShadowRequests, &lightCounts);
        ProbeVolumes.AppendActive(ctx.Partitions, Lights);
    }

    {
        CpuScopeTimer timer(scopes, CpuScope::ShadowGather);
        const bool wantsCasterEvents = Residency.HasOnChangeSlots();
        ShadowCasterExtractor.Extract(
            world, ctx.Partitions, *Meshes, *Materials, *MaterialSets,
            ShadowCasters, wantsCasterEvents, ctx.Presentation.Alpha);

        if (wantsCasterEvents)
        {
            // The retained table is only as fresh as the last frame that built
            // records, so the first frame after a gap adopts the current set as
            // the baseline instead of reporting every caster as new. Events
            // resume the frame after.
            CasterDiff.Apply(ShadowCasters.Records, CasterRecordsWereBuilt, CasterEvents);
        }
        CasterRecordsWereBuilt = wantsCasterEvents;
    }

    {
        CpuScopeTimer timer(scopes, CpuScope::ShadowResidency);
        Residency.Update(ShadowRequests, PointShadowRequests, CasterEvents,
                         EngineConsoleBuiltins::ReadShadowResidencyBudgets(Console));
        Residency.ApplyGrants(Lights);
    }

    if (Instrumentation != nullptr && Instrumentation->Stats != nullptr)
        PublishExtractionStats(*Instrumentation->Stats, lightCounts);

    if (Log != nullptr)
    {
        // Filling the cap exactly drops nothing; only candidates beyond it do.
        const std::uint32_t dropped = lightCounts.DroppedAtCap();
        if (dropped > 0 && !LightCapWarned)
        {
            Log->Warn("Light cap ({}) reached; {} light(s) dropped this frame",
                      kMaxForwardLights, dropped);
            LightCapWarned = true;
        }
        else if (dropped == 0)
        {
            LightCapWarned = false;
        }
    }

}
