#include <app/LoadedLevel.h>

#include <physics/SensorOverlaps.h>

#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/Logger.h>
#include <navigation/NavigationFile.h>
#include <navigation/ZoneNavigation.h>
#include <ecs/World.h>
#include <physics/ZoneCollisionLoader.h>
#include <render/ProbeVolumeSet.h>
#include <render/ZonePresentation.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/HeldRecords.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

namespace
{
[[nodiscard]] ZoneStateScope StateScopeOf(RuntimeWorld& runtime, const LevelReference& level, ZoneId zone)
{
    ZoneStateStore* store = runtime.Entities().TryGetResource<ZoneStateStore>();
    return store != nullptr ? store->Scope(level, zone) : ZoneStateScope{};
}

// A cooked-manifest scene ref ("<.cooked-relative or root-relative path>") as
// the asset:// path the cooked scan registered it under.
[[nodiscard]] std::string CookedRefToAssetPath(std::string_view ref)
{
    constexpr std::string_view cookedPrefix = ".cooked/";
    if (ref.starts_with(cookedPrefix))
        ref.remove_prefix(cookedPrefix.size());
    return AssetPathOf(ref);
}
} // namespace

LoadedLevel::LoadedLevel(Engine& engine, RuntimeContent& content, Logger& log)
    : Host(engine)
    , Content(content)
    , Log(log)
{
    Preloader.emplace(
        engine.Logging(),
        content.Assets().Registry,
        content.Assets().Assets,
        engine.Tasks());
    ZoneLoader.emplace(
        engine.Tasks(),
        engine.World(),
        engine.RuntimeComponents(),
        engine.SceneSerializers(),
        content.SceneContext(),
        engine.Runtime());
    engine.World().Entities().SetResource(ZonePlacement{
        .PartitionAt = [this](const Vec3d& point) -> std::optional<StoragePartitionId> {
            const ZoneId zone = ZoneAt(point);
            const RuntimeZoneRecord* record = zone.IsValid() ? Host.World().FindZone(zone) : nullptr;
            return record != nullptr ? std::optional<StoragePartitionId>(record->Partition) : std::nullopt;
        } });
}

LoadedLevel::~LoadedLevel()
{
    Host.World().Entities().SetResource(ZonePlacement{});
}

std::string LoadedLevel::WorldManifestPath(std::string_view worldName) const
{
    return CookedRoot() + "/worlds/" + std::string(worldName) + ".sworld.json";
}

std::string LoadedLevel::CookedRoot() const
{
    const std::span<const ContentRootPaths> roots = Content.Roots();
    if (roots.empty())
        return std::string(".cooked");
    return roots.front().Cooked.string();
}

bool LoadedLevel::IsLoaded() const
{
    return SceneLoaded || (Partition.has_value() && Partition->HasManifest());
}

const WorldPartitionManifest* LoadedLevel::Manifest() const
{
    return Partition.has_value() && Partition->HasManifest() ? &Partition->Manifest() : nullptr;
}

LevelResidency LoadedLevel::Residency() const
{
    if (!Current.has_value())
        return LevelResidency::Unloaded;
    const ZoneId zone = PlayZone();
    const RuntimeZoneRecord* record = zone.IsValid() ? Host.World().FindZone(zone) : nullptr;
    if (record != nullptr && record->State == RuntimeZoneLoadState::Resident)
        return LevelResidency::Resident;
    if (SceneLoaded && record == nullptr && !ZoneLoader->IsLoading(kSceneLevelZone))
        return LevelResidency::Failed;
    return LevelResidency::Loading;
}

bool LoadedLevel::IsCooked(const LevelReference& level) const
{
    if (level.Kind == LevelKind::Scene)
        return Content.Assets().Assets.Resolve(AssetPathOf(level.Name + ".smap"), AssetType::Scene) != nullptr;
    std::error_code ignored;
    return std::filesystem::is_regular_file(WorldManifestPath(level.Name), ignored);
}

void LoadedLevel::CaptureState()
{
    RuntimeWorld& runtime = Host.World();
    const auto capture = [&](ZoneId zone) {
        if (const RuntimeZoneRecord* record = runtime.FindZone(zone);
            record != nullptr && record->State == RuntimeZoneLoadState::Resident)
            CaptureZoneState(runtime.Entities(), record->StateScope,
                             IdentifiedEntitiesIn(runtime.Entities(), record->Partition), ZoneCapture::Live);
    };
    if (SceneLoaded)
        capture(kSceneLevelZone);
    if (Partition.has_value() && Partition->HasManifest())
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            capture(header.Id);
    if (!WorldSceneEntities.empty())
        CaptureZoneState(runtime.Entities(), WorldSceneScope, WorldSceneEntities, ZoneCapture::Live);
}

ZoneStateScope LoadedLevel::ScopeOfEntity(EntityId entity) const
{
    const RuntimeWorld& runtime = Host.World();
    if (!runtime.Entities().IsAlive(entity))
        return {};
    if (const RuntimeZoneRecord* record = runtime.FindPartition(runtime.Entities().GetEntityPartition(entity)))
        return record->StateScope;
    return std::ranges::find(WorldSceneEntities, entity) != WorldSceneEntities.end() ? WorldSceneScope
                                                                                     : ZoneStateScope{};
}

ZoneId LoadedLevel::ZoneAt(const Vec3d& point) const
{
    const RuntimeWorld& runtime = Host.World();
    const auto resident = [&](ZoneId zone) {
        const RuntimeZoneRecord* record = zone.IsValid() ? runtime.FindZone(zone) : nullptr;
        return record != nullptr && record->State == RuntimeZoneLoadState::Resident;
    };
    if (SceneLoaded)
        return resident(kSceneLevelZone) ? kSceneLevelZone : ZoneId{};
    if (!Partition.has_value() || !Partition->HasManifest())
        return ZoneId{};
    if (const ZoneHeader* innermost = InnermostZoneAt(Partition->Manifest(), point,
                                                      [&](const ZoneHeader& header) { return resident(header.Id); }))
        return innermost->Id;
    const ZoneId focus = Partition->FocusZone();
    return resident(focus) ? focus : ZoneId{};
}

ZoneId LoadedLevel::PlayZone() const
{
    if (SceneLoaded)
        return kSceneLevelZone;
    if (Partition.has_value() && Partition->HasManifest())
        return Partition->FocusZone();
    return ZoneId{};
}

void LoadedLevel::ConnectCollision(CollisionShapeCache& shapes)
{
    PhysicsShapes = &shapes;
    if (PendingCollision.empty())
        return;

    LoadZoneCollision(
        Host.World().Entities(),
        *PhysicsShapes,
        PendingCollision,
        CookedRoot(),
        PersistentStoragePartition);
    PendingCollision.clear();
}

// A streamed scene's cooked siblings, read on the task thread beside the scene
// parse and attached on the owner thread while the zone is still hidden.
struct StagedSceneContent
{
    ProbeVolumeFile Probes;
    std::optional<NavigationFile> Navigation;
    std::string NavigationError;
};

// A streamed scene's cooked content, attached while the zone is still hidden:
// collision from the cells the .smap carries, probes and navigation from the
// sibling cooked files. The one body both a scene load and every world-zone
// recipe share.
void LoadedLevel::AttachSceneContent(RuntimeWorld& runtime,
                                     RuntimeZoneRecord& zone,
                                     const SmapContents& contents,
                                     StagedSceneContent& staged)
{
    if (PhysicsShapes != nullptr)
    {
        LoadZoneCollision(
            runtime.Entities(),
            *PhysicsShapes,
            contents.Collision,
            CookedRoot(),
            zone.Partition);
    }
    if (DefaultRenderPipeline* pipeline = Host.GetRenderPipeline())
        AttachZoneProbes(pipeline->GetProbeVolumes(), zone, staged.Probes);
    AttachZonePresentation(zone, contents.Presentation);
    if (!staged.NavigationError.empty())
        Log.Warn("level: navigation not loaded: {}", staged.NavigationError);
    if (staged.Navigation.has_value())
    {
        const ZoneNavigation* navigation =
            AttachZoneNavigation(runtime, zone, std::move(*staged.Navigation));
        staged.Navigation.reset();
        if (navigation == nullptr)
            Log.Warn("level: zone {:016x} navigation has no loadable profile", zone.Id.Value);
        else
            for (const std::string& diagnostic : navigation->Diagnostics())
                Log.Warn("level: zone {:016x}: {}", zone.Id.Value, diagnostic);
    }
}

// The task-thread half beside the scene parse: file IO for the scene's cooked
// siblings against the cooked-scene path convention.
AsyncZoneLoader::SceneStageFn LoadedLevel::MakeContentStage(
    std::string sceneFilePath, std::shared_ptr<StagedSceneContent> staged)
{
    return [staged = std::move(staged),
            sceneFilePath = std::move(sceneFilePath)](const SmapContents&)
    {
        (void)ReadZoneProbeFile(sceneFilePath, staged->Probes);
        NavigationFile navigation;
        if (ReadZoneNavigationFile(sceneFilePath, navigation, &staged->NavigationError))
            staged->Navigation = std::move(navigation);
    };
}

ConsoleResult LoadedLevel::LoadScene(std::string_view mapName)
{
    RuntimeAssets& assets = Content.Assets();
    ConsoleResult result;

    if (Partition && Partition->HasManifest())
    {
        result.Error("a partitioned world is already loaded");
        return result;
    }
    if (Host.World().FindZone(kSceneLevelZone) != nullptr
        || ZoneLoader->IsLoading(kSceneLevelZone))
    {
        result.Error("a map is already loaded or loading");
        return result;
    }

    const std::string sceneAssetPath =
        AssetPathOf(std::string(mapName) + ".smap");
    const AssetRecord* sceneRecord =
        assets.Assets.Resolve(sceneAssetPath, AssetType::Scene);
    if (sceneRecord == nullptr)
    {
        result.Error("no cooked map at '" + sceneAssetPath
                     + "'; cook the level first");
        return result;
    }
    const std::string sceneFilePath = sceneRecord->FilePath;

    // Warm the scene's dependency table before the load; a metadata read that
    // fails leaves the slower resolve-on-import fallback, not an error.
    std::string preloadError;
    std::shared_ptr<AssetPreload> preload =
        Preloader->BeginSceneDependencies(sceneFilePath, &preloadError);
    if (preload == nullptr)
    {
        Log.Warn("level: no preload for '{}' ({}); resolve-on-import",
                 std::string(mapName),
                 preloadError);
    }

    const LevelReference level{ .Kind = LevelKind::Scene, .Name = std::string(mapName) };
    auto staged = std::make_shared<StagedSceneContent>();
    const AsyncTaskHandle load = ZoneLoader->BeginLoadScene(
        kSceneLevelZone,
        sceneAssetPath,
        assets.Assets,
        assets.Scenes,
        MakeContentStage(sceneFilePath, staged),
        [this, staged](
            RuntimeWorld& runtime,
            RuntimeZoneRecord& zone,
            const SmapContents& contents)
        {
            AttachSceneContent(runtime, zone, contents, *staged);
            return true;
        },
        ZoneParticipation{
            .Visible = true,
            .Physics = true,
            .Logic = true,
            .Audio = true,
        },
        std::move(preload),
        StateScopeOf(Host.World(), level, kSceneLevelZone));
    if (!load.IsValid())
    {
        result.Error("map load refused; see zone load failures");
        return result;
    }

    SceneLoaded = true;
    Current = level;
    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

ConsoleResult LoadedLevel::LoadWorld(std::string_view worldName,
                                     std::optional<ZoneId> initialFocus)
{
    ConsoleResult result;

    if (SceneLoaded || ZoneLoader->IsLoading(kSceneLevelZone))
    {
        result.Error("a map is loaded; restart and use +world");
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a world is already loaded");
        return result;
    }

    const std::string manifestPath = WorldManifestPath(worldName);
    std::string parseError;
    const std::optional<JsonValue> json =
        JsonParseFile(manifestPath, &parseError);
    if (!json)
    {
        result.Error("world manifest: " + parseError);
        return result;
    }

    std::string manifestError;
    std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*json, &manifestError);
    if (!manifest)
    {
        result.Error("world manifest rejected: " + manifestError);
        return result;
    }

    RuntimeAssets& assets = Content.Assets();
    const std::string authoredRoot =
        Content.Roots().empty() ? std::string(".")
                                : Content.Roots().front().Authored.string();
    const EngineRuntimeConfig& runtimeConfig = Host.Config().Runtime;
    const LevelReference level{ .Kind = LevelKind::World, .Name = std::string(worldName) };

    Partition.emplace(
        [this, assets = &assets, authoredRoot, level](const ZoneHeader& header)
        {
            const std::string scenePath = authoredRoot + "/" + header.CookedSceneRef;
            auto staged = std::make_shared<StagedSceneContent>();

            ZoneLoadRecipe recipe;
            // Warm the zone's assets (meshes, materials, the lightmap atlas)
            // before attach, from the .smap's own dependency table. A failed
            // metadata read = resolve-on-attach fallback.
            if (Preloader.has_value())
                recipe.Preload = Preloader->BeginSceneDependencies(scenePath);

            ZoneSceneRecipe scene;
            scene.AssetPath = CookedRefToAssetPath(header.CookedSceneRef);
            scene.Assets = &assets->Assets;
            scene.Scenes = &assets->Scenes;
            scene.StageExtra = MakeContentStage(scenePath, staged);
            scene.Finalize =
                [this, staged](
                    RuntimeWorld& runtime,
                    RuntimeZoneRecord& zone,
                    const SmapContents& contents)
                {
                    AttachSceneContent(runtime, zone, contents, *staged);
                    return true;
                };
            recipe.Scene = std::move(scene);
            recipe.StateScope = StateScopeOf(Host.World(), level, header.Id);
            return recipe;
        },
        WorldPartitionStreamingConfig{
            .HopCount = runtimeConfig.StreamingHopCount,
            .LingerSeconds = runtimeConfig.StreamingLingerSeconds,
            .ResidentZoneCap = runtimeConfig.StreamingResidentZoneCap,
            .NeighborVisible = runtimeConfig.StreamingNeighborVisible,
            .NeighborPhysics = runtimeConfig.StreamingNeighborPhysics,
            .Radius = runtimeConfig.StreamingRadius,
        });

    std::string loadError;
    if (!Partition->LoadManifest(std::move(*manifest), &loadError))
    {
        Partition.reset();
        result.Error("world refused: " + loadError);
        return result;
    }
    // Copied, not referenced: every failure below drops the partition that owns
    // the manifest, and the message names the scene that failed.
    const std::string worldSceneRef = Partition->Manifest().CookedWorldSceneRef;
    if (!worldSceneRef.empty())
    {
        // Synchronous through the front door: the world scene loads once at
        // world start, so the async lane buys nothing here, and residency
        // means a later spawn of the same scene shares the parse.
        // The imported entities are the product; the parse is scaffolding
        // that the lease lets go of on every path out of this block.
        const AssetLease worldScene = assets.Assets.LoadLease(
            CookedRefToAssetPath(worldSceneRef), AssetType::Scene);
        if (!worldScene.IsValid())
        {
            Partition.reset();
            result.Error("world scene '" + worldSceneRef + "' failed to load");
            return result;
        }
        const SmapContents* contents = assets.Scenes.Get(
            SceneHandle::FromToken(worldScene.OpaqueToken()));

        EntityBuildPackage package;
        SmapError buildError;
        if (!BuildEntityPackageFromSmap(*contents, Host.SceneSerializers(),
                                        package, &buildError))
        {
            Partition.reset();
            result.Error("world scene load error: " + buildError.Message);
            return result;
        }

        WorldSceneScope = StateScopeOf(Host.World(), level, kWorldSceneZone);
        ZoneImportError importError;
        if (!ImportPackageIntoPartition(
                Host.World().Entities(),
                Host.RuntimeComponents(),
                package,
                PersistentStoragePartition,
                WorldSceneScope,
                Host.SceneSerializers(),
                Content.SceneContext(),
                &importError,
                &WorldSceneEntities))
        {
            WorldSceneEntities.clear();
            Partition.reset();
            result.Error("world scene import error: " + importError.Message);
            return result;
        }

        if (PhysicsShapes != nullptr)
        {
            LoadZoneCollision(
                Host.World().Entities(),
                *PhysicsShapes,
                contents->Collision,
                CookedRoot(),
                PersistentStoragePartition);
        }
        else if (!contents->Collision.empty())
        {
            PendingCollision = contents->Collision;
        }
    }

    const auto zoneExists = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            if (header.Id == zone)
                return true;
        return false;
    };
    ZoneId focus = initialFocus.value_or(ZoneId{});
    if (!focus.IsValid() || !zoneExists(focus))
        focus = Partition->Manifest().StartZone;
    if (focus.IsValid() && zoneExists(focus))
        Partition->SetFocus(focus);

    // Handed over once. From here the engine keeps the world loaded around
    // whoever this machine drives and, in a session, around every connected
    // player -- and offers each peer only its own neighbourhood.
    Host.SetWorldStreaming(&*Partition, &*ZoneLoader);
    Current = level;

    result.Info("loading world '" + std::string(worldName) + "'");
    return result;
}

ConsoleResult LoadedLevel::FocusZone(ZoneId zone)
{
    ConsoleResult result;
    if (!Partition || !Partition->HasManifest())
    {
        result.Error("no world loaded (use `world <name>`)");
        return result;
    }

    for (const ZoneHeader& header : Partition->Manifest().Zones)
    {
        if (header.Id == zone)
        {
            Partition->SetFocus(zone);
            result.Info("focus zone " + ZoneIdToString(zone));
            return result;
        }
    }
    result.Error("zone " + ZoneIdToString(zone)
                 + " is not in the loaded world");
    return result;
}

ConsoleResult LoadedLevel::DescribeZones() const
{
    ConsoleResult result;
    if (!Partition || !Partition->HasManifest())
    {
        result.Info("no world loaded (use `world <name>`)");
        return result;
    }

    RuntimeWorld& runtime = Host.World();
    const auto zoneName = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            if (header.Id == zone)
                return header.Name;
        return ZoneIdToString(zone);
    };

    result.Info("focus: " + zoneName(Partition->FocusZone()));
    for (const ZoneDemandRecord& record : Partition->DemandRecords())
    {
        const std::string sources = DescribeZoneDemandReasons(record);

        std::string state = "unloaded";
        if (const RuntimeZoneRecord* zone = runtime.FindZone(record.Zone))
        {
            if (zone->State == RuntimeZoneLoadState::Importing)
                state = "loading";
            else
                state = zone->Participation.Any() ? "live" : "dormant";
        }
        else if (ZoneLoader && ZoneLoader->IsLoading(record.Zone))
        {
            state = "loading";
        }

        result.Info("  " + zoneName(record.Zone) + ": " + sources + ", " + state);
    }
    return result;
}

void LoadedLevel::Unload(UnloadedState state)
{
    RuntimeWorld& runtime = Host.World();
    const bool captured = state == UnloadedState::Captured;

    if (ZoneLoader)
    {
        if (ZoneLoader->IsLoading(kSceneLevelZone))
            (void)ZoneLoader->CancelLoad(kSceneLevelZone);
        if (Partition && Partition->HasManifest())
        {
            for (const ZoneHeader& zone : Partition->Manifest().Zones)
                if (ZoneLoader->IsLoading(zone.Id))
                    (void)ZoneLoader->CancelLoad(zone.Id);
        }
    }

    const auto detach = [&](ZoneId zone) {
        if (RuntimeZoneRecord* record = runtime.FindZone(zone))
        {
            record->CapturedAtDetach = captured;
            (void)runtime.RequestDetach(zone);
        }
    };
    detach(kSceneLevelZone);
    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& zone : Partition->Manifest().Zones)
            detach(zone.Id);
    }
    runtime.FlushLifecycleRequests();

    // The detaches above are published like any other residency change, so a
    // game's zone-residency systems see their content leaving rather than
    // finding it gone.
    const std::span<const ZoneResidencyChange> changes =
        runtime.BeginResidencyProcessing();
    ZoneResidencyContext residency{
        .Config = Host.Config(),
        .Entities = runtime.Entities(),
        .Changes = changes,
    };
    Host.Schedule().RunZoneResidency(residency);
    runtime.FinalizeResidencyProcessing();

    std::erase_if(WorldSceneEntities, [&](EntityId entity) {
        return runtime.Entities().IsAlive(entity)
            && runtime.Entities().GetEntityPartition(entity) != PersistentStoragePartition;
    });
    ParkHeldRecords(runtime.Entities(), WorldSceneScope, WorldSceneEntities);
    if (captured)
        CaptureZoneState(runtime.Entities(), WorldSceneScope, WorldSceneEntities, ZoneCapture::Detach);
    // Only what a world scene imported. The persistent partition also holds
    // whatever the game put there, and this owns none of that.
    for (EntityId entity : WorldSceneEntities)
    {
        if (runtime.Entities().IsAlive(entity))
            runtime.Entities().DestroyEntity(entity);
    }
    WorldSceneEntities.clear();
    WorldSceneScope = {};
    if (SensorOverlaps* overlaps = runtime.Entities().TryGetResource<SensorOverlaps>())
        overlaps->ForgetAwaitedIf([](const SensorSeed&) { return true; });

    // Before the runtime it points at goes.
    Host.SetWorldStreaming(nullptr, nullptr);
    Partition.reset();
    PendingCollision.clear();
    SceneLoaded = false;
    Current.reset();
}
