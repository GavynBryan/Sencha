#include <app/DefaultRenderPipeline.h>

#include <app/EngineConsoleBuiltins.h>
#include <core/console/ConsoleRegistry.h>
#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderStats.h>
#include <world/transform/TransformComponents.h>

#ifdef SENCHA_ENABLE_VULKAN
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <render/MeshRenderFeature.h>
#include <render/ShadowRenderFeature.h>
#endif

#include <algorithm>
#include <memory>
#include <variant>

namespace
{
    float ReadDoubleCVar(const ConsoleRegistry* console,
                         std::string_view name,
                         float fallback)
    {
        if (console == nullptr)
            return fallback;
        const CVarMetadata* metadata = console->FindCVar(name);
        if (metadata == nullptr)
            return fallback;
        const double* value = std::get_if<double>(&metadata->CurrentValue);
        return value != nullptr ? static_cast<float>(*value) : fallback;
    }

    bool ReadBoolCVar(const ConsoleRegistry* console,
                      std::string_view name,
                      bool fallback)
    {
        if (console == nullptr)
            return fallback;
        const CVarMetadata* metadata = console->FindCVar(name);
        if (metadata == nullptr)
            return fallback;
        const bool* value = std::get_if<bool>(&metadata->CurrentValue);
        return value != nullptr ? *value : fallback;
    }

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    RenderDebugView ReadDebugViewCVar(const ConsoleRegistry* console)
    {
        if (console == nullptr)
            return RenderDebugView::None;
        const CVarMetadata* metadata = console->FindCVar("render.debug.view");
        if (metadata == nullptr)
            return RenderDebugView::None;
        const std::string* value = std::get_if<std::string>(&metadata->CurrentValue);
        RenderDebugView view = RenderDebugView::None;
        if (value != nullptr)
            (void)ParseRenderDebugView(*value, view);
        return view;
    }
#endif

    void ApplyRendererCVars(const ConsoleRegistry* console, RenderLightSet& lights)
    {
        lights.AmbientSky = Vec<3>(
            ReadDoubleCVar(console, "render.ambient.sky_r", lights.AmbientSky.X),
            ReadDoubleCVar(console, "render.ambient.sky_g", lights.AmbientSky.Y),
            ReadDoubleCVar(console, "render.ambient.sky_b", lights.AmbientSky.Z));
        lights.AmbientGround = Vec<3>(
            ReadDoubleCVar(console, "render.ambient.ground_r", lights.AmbientGround.X),
            ReadDoubleCVar(console, "render.ambient.ground_g", lights.AmbientGround.Y),
            ReadDoubleCVar(console, "render.ambient.ground_b", lights.AmbientGround.Z));
        lights.DiffuseWrap = ReadDoubleCVar(
            console, "render.style.diffuse_wrap", lights.DiffuseWrap);
        lights.MinAmbient = ReadDoubleCVar(
            console, "render.style.min_ambient", lights.MinAmbient);
        lights.Exposure = ReadDoubleCVar(console, "render.exposure", lights.Exposure);
        lights.TonemapKnee = ReadDoubleCVar(
            console, "render.tonemap.knee", lights.TonemapKnee);
        lights.TonemapEnabled = ReadBoolCVar(
            console, "render.tonemap", lights.TonemapEnabled);
        lights.ShadowDarkness = ReadDoubleCVar(
            console, "render.shadow.darkness", lights.ShadowDarkness);
        lights.ShadowSoftness = ReadDoubleCVar(
            console, "render.shadow.softness", lights.ShadowSoftness);
        lights.ShadowBiasConstant = ReadDoubleCVar(
            console, "render.shadow.bias_const", lights.ShadowBiasConstant);
        lights.ShadowBiasSlope = ReadDoubleCVar(
            console, "render.shadow.bias_slope", lights.ShadowBiasSlope);
        lights.BakedDirectEnabled = ReadBoolCVar(
            console, "render.baked_direct.enabled", lights.BakedDirectEnabled);
        lights.BakedAoEnabled = ReadBoolCVar(
            console, "render.ao.enabled", lights.BakedAoEnabled);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
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
                                           TextureCache* textures)
{
    Meshes = &meshes;
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

    // The shadow feature renders the atlas the forward pass samples. Adding it
    // first runs its Setup first, so the lighting set layout exists when the
    // forward pass builds its pipeline layout; Offscreen also records before
    // MainColor, so tiles are written before they are read.
    auto bindings = std::make_shared<LightBindings>();
    if (graphics.MainRenderer.AddFeature(std::make_unique<ShadowRenderFeature>(
            bindings, Lights, ShadowCasters, *Meshes, Residency)) == nullptr)
    {
        return false;
    }
    // Probe residency shares the lighting set: it swaps binding-2 slots as
    // zones stream and hands headers to extraction. Uploads only ever happen
    // after the shadow feature's Setup has created the set.
    ProbeVolumes.Setup(&graphics.Images, bindings, Logging);
    return graphics.MainRenderer.AddFeature(std::make_unique<MeshRenderFeature>(
        Queue, *Meshes, *Materials, Camera, Lights, std::move(bindings))) != nullptr;
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

void DefaultRenderPipeline::ExtractRender(RenderExtractContext& ctx)
{
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

    Queue.Reset();

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
            RenderExtractCaches{ *Meshes, *Materials, *MaterialSets, Textures },
            Camera, Queue, ctx.Presentation.Alpha);
        Queue.SortOpaque();
    }

    LightExtractionCounts lightCounts;
    {
        CpuScopeTimer timer(scopes, CpuScope::LightSelection);
        Lights.Reset();
        ApplyRendererCVars(Console, Lights);
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

        CasterEvents.clear();
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
