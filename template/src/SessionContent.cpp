#include "SessionContent.h"

#include "GameSettingsData.h"
#include "PawnSpawn.h"
#include "PlayerAvatarData.h"
#include "TemplateInputActions.h"

#include <anim/AnimationClipPlaybackRuntime.h>
#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <audio/AudioSourceRuntime.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <controller/ControllerRegistration.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetStoreTable.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <input/InputBindingCache.h>
#include <controller/LookOrientation.h>
#include <input/InputRegistration.h>
#include <movement/MovementProfileBindingCache.h>
#include <movement/MovementRegistration.h>
#include <participant/LocalControl.h>
#include <physics/CharacterMoverPool.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/ZoneCollisionLoader.h>
#include <render/ProbeVolumeSet.h>
#include <runtime/spawn/NetPrefabSpawner.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZonePackageImporter.h>

#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/MovementStatePanel.h>
#endif

#include <cassert>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view kAuthoredRoot = "assets";
constexpr std::string_view kCookedScanRoot = "assets/.cooked";
constexpr std::string_view kPlayerAvatarPath =
    "asset://data/player_avatar.sdata";
constexpr std::string_view kInputActionSetPath =
    "asset://data/input_actions.sdata";
constexpr std::string_view kInputProfilePath =
    "asset://data/input_default.sdata";
constexpr std::string_view kGameSettingsPath = "asset://data/game.sdata";
constexpr ZoneId kPlayZone{ 1 };


// A cooked-manifest scene ref ("<.cooked-relative or root-relative path>")
// as the asset:// path the cooked scan root registered it under.
[[nodiscard]] std::string CookedRefToAssetPath(std::string_view ref)
{
    constexpr std::string_view cookedPrefix = ".cooked/";
    if (ref.starts_with(cookedPrefix))
        ref.remove_prefix(cookedPrefix.size());
    return "asset://" + std::string(ref);
}

void ConfigureRuntimeResources(
    Engine& engine,
    RuntimeAssets& assets)
{
    World& world = engine.World().Entities();

    world.SetResource(assets.Assets.Stores());
    world.SetResource(AudioSourceRuntime{
        &assets.AudioClips, &engine.Audio(), &engine.Captions() });
    world.SetResource(AnimationClipPlaybackRuntime{ &assets.AnimationClips });

    RegisterPhysicsComponents(world);
    RegisterMovement(world);
    RegisterCameraComponents(world);
    RegisterControllerComponents(world);
}

struct WorldPartitionUpdateSystem
{
    explicit WorldPartitionUpdateSystem(
        std::optional<WorldPartitionRuntime>& partition)
        : Partition(partition)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        (void)ctx;
        if (!Partition || !Partition->HasManifest())
            return;

        // Streaming itself is the engine's: it was handed this partition when
        // the world loaded and drives it in the zone-residency phase.
        //
        // What is left here is a gameplay decision. A crossing the destination
        // is not ready for leaves the pawn where the sweep last had it fully
        // inside the room it is leaving; streaming decides that on the wall
        // clock, but moving a pawn is simulation, so the position is recorded
        // and applied on the next fixed tick.
        if (LocalControlSubjectOf(ctx.Entities).IsValid()
            && Partition->LastTraversal().Status
                == DockTraversalStatus::BlockedDestinationNotReady)
        {
            PendingSafePosition = Partition->LastTraversal().SafeSourcePosition;
        }
    }

    // Applied at the head of the tick, before movement runs, so the pawn never
    // enters physics at the position that reached into the unloaded zone.
    void FixedLogic(FixedLogicContext& ctx)
    {
        if (!PendingSafePosition.has_value())
            return;

        World& world = ctx.Entities;
        const EntityId pawn = LocalControlSubjectOf(world);
        if (!pawn.IsValid())
        {
            PendingSafePosition.reset();
            return;
        }

        const Vec3d safe = *PendingSafePosition;
        PendingSafePosition.reset();

        // Through the mover, never onto the transform alone: a character's
        // position lives inside its mover and the transform is where the last
        // sweep left a copy, so writing the copy is undone by the next tick.
        bool moved = false;
        if (Movers != nullptr)
            moved = Movers->SetPosition(world, pawn, safe);
        if (!moved)
        {
            if (LocalTransform* transform = world.TryGet<LocalTransform>(pawn))
                transform->Value.Position = safe;
            RequestTransformHistorySnap(world, pawn);
        }
        if (WorldTransform* transform = world.TryGet<WorldTransform>(pawn))
            transform->Value.Position = safe;
    }

    std::optional<WorldPartitionRuntime>& Partition;
    // Owned by the physics step, which is where characters live. Null in a
    // configuration with no physics, where the transform is all there is.
    CharacterMoverPool* Movers = nullptr;
    // Set by streaming on the wall clock, consumed by the next fixed tick.
    std::optional<Vec3d> PendingSafePosition;
};

#ifdef SENCHA_ENABLE_COOK
// Polls the source watcher on wall time and hands changed files to the
// reloader, which stages an in-place swap that commits at the async drain.
struct HotReloadPollSystem
{
    HotReloadPollSystem(
        std::optional<AssetSourceWatcher>& watcher,
        std::optional<AssetHotReloader>& reloader)
        : Watcher(watcher)
        , Reloader(reloader)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Watcher.has_value() || !Reloader.has_value())
            return;

        Accumulator += ctx.WallDeltaSeconds;
        if (Accumulator < kPollIntervalSeconds)
            return;
        Accumulator = 0.0;

        for (const std::string& changed : Watcher->PollChanged())
            Reloader->ReloadSource(changed);
    }

    static constexpr double kPollIntervalSeconds = 0.3;
    std::optional<AssetSourceWatcher>& Watcher;
    std::optional<AssetHotReloader>& Reloader;
    double Accumulator = 0.0;
};
#endif
} // namespace

void RegisterTemplateDataTypes(DataAssetTypeRegistry& types,
                               DataSchemaRegistry& schemas)
{
    RegisterPlayerAvatarData(types, schemas);
    RegisterGameSettingsData(types, schemas);
}

void UnregisterTemplateDataTypes(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas)
{
    UnregisterGameSettingsData(types, schemas);
    UnregisterPlayerAvatarData(types, schemas);
}

SessionContent::SessionContent(Engine& engine, Logger& log)
    : Host(engine)
    , Log(log)
{
}

SessionContent::~SessionContent() = default;

void SessionContent::Open()
{
    Engine& engine = Host;
    LoggingProvider& Logging = engine.Logging();
    // A dedicated host has no graphics services, so it composes an asset stack
    // that cannot hold a mesh or a texture and loads everything else -- the
    // movement profiles it simulates from, the collision it collides with --
    // through the same front door.
    GraphicsServices* graphics = engine.TryGraphics();
    if (graphics != nullptr)
    {
        Assets_.emplace(
            Logging,
            graphics->Buffers,
            graphics->Images,
            graphics->Descriptors,
            graphics->Samplers,
            engine.SceneSerializers());
    }
    else
    {
        Assets_.emplace(Logging, engine.SceneSerializers());
    }
    RuntimeAssets& runtimeAssets = Assets();

    // This game's own data subtypes, registered into the registries it owns and
    // unregistered in Close while the module is still mapped: the registry
    // holds function pointers into this module.
    RegisterTemplateDataTypes(runtimeAssets.DataTypes, runtimeAssets.DataSchemas);

    ScanAssetsDirectory(
        std::string(kAuthoredRoot),
        runtimeAssets.Registry,
        runtimeAssets.Assets.Kinds());
    ScanAssetsDirectory(
        std::string(kCookedScanRoot),
        runtimeAssets.Registry,
        runtimeAssets.Assets.Kinds());
    RegisterCookedAssets(
        std::string(kAuthoredRoot),
        runtimeAssets.Registry);

    AssetIdMap idMap;
    std::string idMapError;
    const std::string idMapPath =
        std::string(kAuthoredRoot) + "/"
        + std::string(kAssetIdMapFileName);
    if (AssetIdMap::LoadFromFile(
            idMapPath,
            idMap,
            &idMapError))
    {
        ApplyAssetIds(idMap, runtimeAssets.Registry);
    }
    else
    {
        Log.Warn(
            "TemplateGame: no asset id map ({}); refs resolve by path only",
            idMapError);
    }

    ConfigureRuntimeResources(engine, runtimeAssets);
    SetupInputMapping();
    SceneContext = std::make_unique<SceneSerializationContext>(
        Logging,
        &runtimeAssets.Assets);
    ZoneLoader.emplace(
        engine.Tasks(),
        engine.World(),
        engine.RuntimeComponents(),
        engine.SceneSerializers(),
        *SceneContext,
        engine.Runtime());
    Preloader.emplace(
        Logging,
        runtimeAssets.Registry,
        runtimeAssets.Assets,
        engine.Tasks());

#ifdef SENCHA_ENABLE_COOK
    HotReloader.emplace(
        Logging,
        runtimeAssets.Assets,
        runtimeAssets.Registry,
        HotReloadImporters,
        engine.Tasks(),
        std::string(kAuthoredRoot));
    HotReloadWatcher.emplace(
        Logging,
        std::string(kAuthoredRoot),
        std::vector<std::string>{ ".sdata" });
    HotReloadWatcher->Initialize();
#endif

#ifdef SENCHA_ENABLE_DEBUG_UI
    // The other half of the movement tuning loop: the editor predicts what a
    // profile does, this reports what the running game resolved from it.
    // Composed here rather than by the engine overlay because the world being
    // simulated and the data cache holding the profile are both the game's.
    engine.AddDebugPanel(std::make_unique<MovementStatePanel>(
        engine.World().Entities(), &runtimeAssets.DataAssets));
#endif

    // The pipeline object exists headless -- it is registered unconditionally
    // and its extract hook is simply never dispatched -- so the guard that
    // matters is the graphics services its mesh feature is built from.
    if (DefaultRenderPipeline* pipeline = engine.GetRenderPipeline();
        pipeline != nullptr && graphics != nullptr)
    {
        pipeline->SetAssetStores(
            *runtimeAssets.StaticMeshes,
            runtimeAssets.Materials,
            runtimeAssets.MaterialSets,
            runtimeAssets.Textures.get());
        pipeline->AddMeshRenderFeature(*graphics);
    }

    // The spawn service is engine-owned; the asset stack it resolves scenes
    // through is this one. Close disconnects them before the stack goes.
    engine.Spawns().ConnectAssets(&runtimeAssets.Assets, &runtimeAssets.Scenes);
    // The same content stack, for the spawns a peer names rather than this
    // machine asking for: without it every replicated prefab is unbuildable
    // and every body a client is sent is deferred forever.
    engine.NetPrefabs().ConnectAssets(&runtimeAssets.Assets, &runtimeAssets.Scenes);

}

void SessionContent::Close()
{
    Engine& engine = Host;
    RuntimeWorld& runtime = engine.World();

    if (ZoneLoader)
    {
        if (ZoneLoader->IsLoading(kPlayZone))
            (void)ZoneLoader->CancelLoad(kPlayZone);
        if (Partition && Partition->HasManifest())
        {
            for (const ZoneHeader& zone : Partition->Manifest().Zones)
                if (ZoneLoader->IsLoading(zone.Id))
                    (void)ZoneLoader->CancelLoad(zone.Id);
        }
    }

    if (runtime.FindZone(kPlayZone) != nullptr)
        (void)runtime.RequestDetach(kPlayZone);
    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& zone : Partition->Manifest().Zones)
            if (runtime.FindZone(zone.Id) != nullptr)
                (void)runtime.RequestDetach(zone.Id);
    }
    runtime.FlushLifecycleRequests();

    const std::span<const ZoneResidencyChange> changes =
        runtime.BeginResidencyProcessing();
    ZoneResidencyContext residency{
        .Config = engine.Config(),
        .Entities = runtime.Entities(),
        .Changes = changes,
    };
    engine.Schedule().RunZoneResidency(residency);
    runtime.FinalizeResidencyProcessing();

    const std::vector<EntityId> alive =
        runtime.Entities().GetAliveEntities();
    for (EntityId entity : alive)
    {
        if (runtime.Entities().GetEntityPartition(entity)
            == PersistentStoragePartition)
        {
            runtime.Entities().DestroyEntity(entity);
        }
    }

    runtime.Entities()
        .GetResource<ActiveCameraService>()
        .SetActive(EntityId{});
    runtime.Entities().SetResource(AssetStoreTable{});
    runtime.Entities().SetResource(AudioSourceRuntime{});
    runtime.Entities().SetResource(AnimationClipPlaybackRuntime{});

    PlayZoneActive = false;
    // Before the runtime it points at goes.
    engine.SetWorldStreaming(nullptr, nullptr);
    // Same for the spawn services: Open connected them to this asset stack, and
    // the prefab spawner holds a scene reference per resident prefab for the
    // length of the session. Disconnecting drops those while the caches that
    // issued them are still here.
    engine.Spawns().ConnectAssets(nullptr, nullptr);
    engine.NetPrefabs().ConnectAssets(nullptr, nullptr);
    Partition.reset();
    ZoneLoader.reset();
    SceneContext.reset();
    Preloader.reset();
#ifdef SENCHA_ENABLE_COOK
    HotReloadWatcher.reset();
    HotReloader.reset();
#endif

    // The world-resource binding caches hold leases into this game's data-asset
    // cache; every reference must drop before Assets goes away. A lease that
    // outlives its owner calls through a destroyed vtable when the world tears
    // down, which aborts on the way out rather than at the point of the mistake.
    if (MovementProfileBindingCache* bindings =
            runtime.Entities().TryGetResource<MovementProfileBindingCache>())
    {
        bindings->Clear();
    }
    if (InputBindingCache* bindings =
            runtime.Entities().TryGetResource<InputBindingCache>())
    {
        bindings->Clear();
    }
    // Same rule for the context lease. The game object is a module-static whose
    // destructor runs at dlclose, long after the world that owns the context set,
    // so the lease has to be dropped here while its owner still exists.
    GameplayInput.Reset();
    // Every lease held into the data-asset cache, dropped here. Declaration
    // order alone is not enough: the stack is reset explicitly below, so
    // anything still holding a lease at that point outlives its owner and calls
    // through a destroyed vtable when the module unloads.
    InputActionSetAsset.Reset();
    InputProfileAsset.Reset();
    // The pawns that held their own references are destroyed above, so this
    // drops the last one before the caches go away.
    ReleasePlayerAvatar();
    PlayerAvatarAsset.Reset();
    GameSettingsAsset.Reset();
    // The subtype registration holds a function pointer into this module, and
    // unregistering refuses while values are still resident, so it follows the
    // handles above and precedes the cache going away.
    if (Assets_.has_value())
        UnregisterTemplateDataTypes(Assets_->DataTypes, Assets_->DataSchemas);
    Assets_.reset();
}

// A streamed scene's cooked content, attached while the zone is still hidden:
// collision from the cells the .smap carries, probes from the sibling cooked
// file. The one body both the +map load and every world-zone recipe share.
void SessionContent::RegisterSystems(SystemRegisterContext& ctx)
{
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        PhysicsShapes = &step->GetShapeCache();

    // Collision a world scene carried before physics existed, loaded now that
    // there is somewhere to put it.
    if (PhysicsShapes != nullptr && !PendingWorldSceneCollision.empty())
    {
        LoadZoneCollision(
            Host.World().Entities(),
            *PhysicsShapes,
            PendingWorldSceneCollision,
            std::string(kCookedScanRoot),
            PersistentStoragePartition);
        PendingWorldSceneCollision.clear();
    }

    WorldPartitionUpdateSystem& partitionUpdate =
        ctx.Schedule.Register<WorldPartitionUpdateSystem>(Partition);
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        partitionUpdate.Movers = &step->GetCharacterMovers();
#ifdef SENCHA_ENABLE_COOK
    ctx.Schedule.Register<HotReloadPollSystem>(HotReloadWatcher, HotReloader);
#endif
}

ConsoleResult SessionContent::DescribeZones() const
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

void SessionContent::AttachStreamedSceneContent(RuntimeWorld& runtime,
                                              RuntimeZoneRecord& zone,
                                              const SmapContents& contents,
                                              const ProbeVolumeFile& probes)
{
    if (PhysicsShapes != nullptr)
    {
        LoadZoneCollision(
            runtime.Entities(),
            *PhysicsShapes,
            contents.Collision,
            std::string(kCookedScanRoot),
            zone.Partition);
    }
    if (DefaultRenderPipeline* pipeline = Host.GetRenderPipeline())
        AttachZoneProbes(pipeline->GetProbeVolumes(), zone, probes);
}

// The task-thread half beside the scene parse: probe file IO against the
// cooked-scene path convention.
AsyncZoneLoader::SceneStageFn SessionContent::MakeProbeStage(
    std::string sceneFilePath, std::shared_ptr<ProbeVolumeFile> probes)
{
    return [probes = std::move(probes),
            sceneFilePath = std::move(sceneFilePath)](const SmapContents&)
    {
        (void)ReadZoneProbeFile(sceneFilePath, *probes);
    };
}

ConsoleResult SessionContent::LoadMap(std::string_view mapName)
{
    Engine& engine = Host;
    RuntimeAssets& runtimeAssets = Assets();
    ConsoleResult result;

    if (!ZoneLoader)
    {
        result.Error("runtime zone loader is unavailable");
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a partitioned world is already loaded");
        return result;
    }
    if (engine.World().FindZone(kPlayZone) != nullptr
        || ZoneLoader->IsLoading(kPlayZone))
    {
        result.Error("a map is already loaded or loading");
        return result;
    }

    const std::string sceneAssetPath =
        "asset://" + std::string(mapName) + ".smap";
    const AssetRecord* sceneRecord =
        runtimeAssets.Assets.Resolve(sceneAssetPath, AssetType::Scene);
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
        Log.Warn(
            "TemplateGame: no preload for '{}' ({}); resolve-on-import",
            std::string(mapName),
            preloadError);
    }

    auto probes = std::make_shared<ProbeVolumeFile>();
    const AsyncTaskHandle load = ZoneLoader->BeginLoadScene(
        kPlayZone,
        sceneAssetPath,
        runtimeAssets.Assets,
        runtimeAssets.Scenes,
        MakeProbeStage(sceneFilePath, probes),
        [this, probes](
            RuntimeWorld& runtime,
            RuntimeZoneRecord& zone,
            const SmapContents& contents)
        {
            AttachStreamedSceneContent(runtime, zone, contents, *probes);

            // Where a pawn belongs, not a pawn. Who provides one is the
            // session's decision, taken every frame once this exists: a load
            // that finished after a join would otherwise place a second body
            // beside the one the authority is already simulating.
            PublishPlayContent(runtime.Entities(), zone.Partition);
            RequestBodiesForWaitingParticipants(Host);
            PlayZoneActive = true;
            return true;
        },
        ZoneParticipation{
            .Visible = true,
            .Physics = true,
            .Logic = true,
            .Audio = true,
        },
        std::move(preload));
    if (!load.IsValid())
    {
        result.Error("map load refused; see zone load failures");
        return result;
    }

    result.Info("loading map '" + std::string(mapName) + "'");
    return result;
}

ConsoleResult SessionContent::LoadWorld(std::string_view worldName)
{
    Engine& engine = Host;
    ConsoleResult result;

    if (PlayZoneActive
        || (ZoneLoader && ZoneLoader->IsLoading(kPlayZone)))
    {
        result.Error("a map is loaded; restart and use +world");
        return result;
    }
    if (Partition && Partition->HasManifest())
    {
        result.Error("a world is already loaded");
        return result;
    }

    const std::string manifestPath =
        std::string(kCookedScanRoot) + "/worlds/"
        + std::string(worldName) + ".sworld.json";
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

    RuntimeAssets& runtimeAssets = Assets();

    const EngineRuntimeConfig& runtimeConfig =
        engine.Config().Runtime;
    Partition.emplace(
        [this, assets = &runtimeAssets](const ZoneHeader& header)
        {
            const std::string scenePath =
                std::string(kAuthoredRoot) + "/"
                + header.CookedSceneRef;
            auto probes = std::make_shared<ProbeVolumeFile>();

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
            scene.StageExtra = MakeProbeStage(scenePath, probes);
            scene.Finalize =
                [this, probes](
                    RuntimeWorld& runtime,
                    RuntimeZoneRecord& zone,
                    const SmapContents& contents)
                {
                    AttachStreamedSceneContent(runtime, zone, contents, *probes);
                    return true;
                };
            recipe.Scene = std::move(scene);
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
    if (!Partition->LoadManifest(
            std::move(*manifest),
            &loadError))
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
        const AssetLease worldScene = runtimeAssets.Assets.LoadLease(
            CookedRefToAssetPath(worldSceneRef), AssetType::Scene);
        if (!worldScene.IsValid())
        {
            Partition.reset();
            result.Error("world scene '" + worldSceneRef + "' failed to load");
            return result;
        }
        const SmapContents* contents = runtimeAssets.Scenes.Get(
            SceneHandle::FromToken(worldScene.OpaqueToken()));

        EntityBuildPackage package;
        SmapError buildError;
        if (!BuildEntityPackageFromSmap(*contents, engine.SceneSerializers(),
                                        package, &buildError))
        {
            Partition.reset();
            result.Error("world scene load error: " + buildError.Message);
            return result;
        }

        ZoneImportError importError;
        if (!ImportPackageIntoPartition(
                engine.World().Entities(),
                engine.RuntimeComponents(),
                package,
                PersistentStoragePartition,
                // The world scene lives in the persistent partition but is
                // saved under the play zone, so its state scope is that zone
                // rather than the partition it occupies.
                kPlayZone,
                engine.SceneSerializers(),
                *SceneContext,
                &importError))
        {
            Partition.reset();
            result.Error(
                "world scene import error: " + importError.Message);
            return result;
        }

        if (PhysicsShapes != nullptr)
        {
            LoadZoneCollision(
                engine.World().Entities(),
                *PhysicsShapes,
                contents->Collision,
                std::string(kCookedScanRoot),
                PersistentStoragePartition);
        }
        else if (!contents->Collision.empty())
        {
            PendingWorldSceneCollision = contents->Collision;
        }
    }

    // A world's scene imports into the persistent partition, so that is where
    // a pawn belongs. Providing one is the session's decision.
    PublishPlayContent(engine.World().Entities(), PersistentStoragePartition);
    RequestBodiesForWaitingParticipants(engine);

    ZoneId focus = PendingZoneFocus;
    PendingZoneFocus = ZoneId{};
    const auto zoneExists = [&](ZoneId zone)
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
            if (header.Id == zone)
                return true;
        return false;
    };
    if (!focus.IsValid() || !zoneExists(focus))
        focus = Partition->Manifest().StartZone;
    if (focus.IsValid() && zoneExists(focus))
        Partition->SetFocus(focus);

    // Handed over once. From here the engine keeps the world loaded around
    // whoever this machine drives and, in a session, around every connected
    // player -- and offers each peer only its own neighbourhood.
    engine.SetWorldStreaming(&*Partition, &*ZoneLoader);

    result.Info("loading world '" + std::string(worldName) + "'");
    return result;
}

ConsoleResult SessionContent::FocusZone(
    std::string_view zoneHex)
{
    ConsoleResult result;
    const std::optional<ZoneId> zone =
        ZoneIdFromString(zoneHex);
    if (!zone.has_value())
    {
        result.Error(
            "malformed zone id '" + std::string(zoneHex) + "'");
        return result;
    }

    if (Partition && Partition->HasManifest())
    {
        for (const ZoneHeader& header : Partition->Manifest().Zones)
        {
            if (header.Id == *zone)
            {
                Partition->SetFocus(*zone);
                result.Info(
                    "focus zone " + std::string(zoneHex));
                return result;
            }
        }
        result.Error(
            "zone " + std::string(zoneHex)
            + " is not in the loaded world");
        return result;
    }

    PendingZoneFocus = *zone;
    result.Info("zone focus queued for the next world load");
    return result;
}

RuntimeAssets& SessionContent::Assets()
{
    assert(Assets_.has_value()
           && "the asset stack is composed by Open before anything asks for it");
    return *Assets_;
}

// Loads one structured data asset synchronously and returns an owned lease.
// Returns an invalid handle on any failure, which every caller treats as
// "run without the authored data" rather than as a fatal error.
DataAssetCacheHandle SessionContent::AcquireDataAsset(std::string_view path)
{
    AssetLease lease = Assets().Assets.LoadLease(path, AssetType::Data);
    if (!lease.IsValid())
    {
        Log.Warn("TemplateGame: '{}' did not load; running without it", path);
        return {};
    }

    // The owned handle takes its own reference; the load's goes with the lease
    // at the end of this scope.
    return DataAssetCacheHandle(&Assets().DataAssets,
                                DataAssetHandle::FromToken(lease.OpaqueToken()));
}

// Loads the pawn's movement profile synchronously the first time a pawn
// spawns. The asset is game-lifetime, so the owned lease lives on the game;
// the tuning system's binding cache adds its own reference on first resolve.
// Turns the authored avatar paths into mesh and material-set handles, once.
// Every failure path leaves the result invalid, which spawns a bodyless pawn
// rather than refusing to spawn: a missing body is a content problem, not a
// reason to have no player.
const CompiledGameSettings* SessionContent::GameSettings()
{
    if (!GameSettingsAsset.IsValid())
        GameSettingsAsset = AcquireDataAsset(kGameSettingsPath);
    if (!GameSettingsAsset.IsValid())
        return nullptr;
    const CompiledGameSettings* settings =
        Assets().DataAssets.TryGet<CompiledGameSettings>(
            GameSettingsAsset.GetToken(), "game.settings");
    if (settings == nullptr)
        Log.Warn("TemplateGame: '{}' is not a game.settings", kGameSettingsPath);
    return settings;
}

ResolvedPlayerAvatar SessionContent::PlayerAvatar()
{
    if (Avatar.IsValid())
        return Avatar;

    // A body is something to draw. A process that cannot hold a mesh has no
    // body to give a pawn and is not missing one: the pawn simulates the same
    // either way, and every machine that draws it resolves its own.
    if (!Assets().Assets.HasStore(AssetType::StaticMesh))
        return {};

    if (!PlayerAvatarAsset.IsValid())
        PlayerAvatarAsset = AcquireDataAsset(kPlayerAvatarPath);
    if (!PlayerAvatarAsset.IsValid())
        return {};

    RuntimeAssets& assets = Assets();
    const CompiledPlayerAvatar* avatar =
        assets.DataAssets.TryGet<CompiledPlayerAvatar>(
            PlayerAvatarAsset.GetToken(), "player.avatar");
    if (avatar == nullptr)
    {
        Log.Warn("TemplateGame: '{}' is not a player.avatar", kPlayerAvatarPath);
        return {};
    }
    AssetLease mesh = assets.Assets.LoadLease(avatar->MeshPath, AssetType::StaticMesh);
    if (!mesh.IsValid())
    {
        Log.Warn("TemplateGame: player avatar mesh '{}' did not load",
                 avatar->MeshPath);
        return {};
    }

    // Each material is held only until the set takes its own reference.
    std::vector<AssetLease> materials;
    std::vector<std::uint64_t> materialTokens;
    for (const std::string& path : avatar->MaterialPaths)
    {
        AssetLease material = assets.Assets.LoadLease(path, AssetType::Material);
        if (!material.IsValid())
        {
            Log.Warn("TemplateGame: player avatar material '{}' did not load",
                     path);
            return {};
        }
        materialTokens.push_back(material.OpaqueToken());
        materials.push_back(std::move(material));
    }

    AssetLease set = assets.Assets.InternList(AssetType::Material, materialTokens);
    if (!set.IsValid())
    {
        Log.Warn("TemplateGame: player avatar materials did not form a set");
        return {};
    }

    Avatar = ResolvedPlayerAvatar{
        .Mesh = StaticMeshHandle::FromToken(mesh.Relinquish()),
        .Materials = MaterialSetHandle::FromToken(set.Relinquish()),
    };
    return Avatar;
}

void SessionContent::ReleasePlayerAvatar()
{
    if (Assets_.has_value())
    {
        Assets_->Assets.ReleaseLease(AssetType::Material, Avatar.Materials.ToToken(),
                                     AssetArity::List);
        Assets_->Assets.ReleaseLease(AssetType::StaticMesh, Avatar.Mesh.ToToken());
    }
    Avatar = {};
}

// Binds the game's controls. The action set loads first: a profile names its
// actions, and binding cannot resolve those names until the set is resident.
void SessionContent::SetupInputMapping()
{
    World& world = Host.World().Entities();

    InputActionSetAsset = AcquireDataAsset(kInputActionSetPath);
    InputProfileAsset = AcquireDataAsset(kInputProfilePath);
    if (!InputProfileAsset.IsValid())
    {
        Log.Error("TemplateGame: no input profile; the game has no controls");
        return;
    }

    const InputProfileHandle profile{ InputProfileAsset.GetToken() };
    RegisterInputMapping(world, Assets().DataAssets, profile);

    // Names resolve to ids once, here. An id outlives a reload of the action
    // set, so every system downstream indexes by id from now on; the resolve
    // system reports whatever failed to bind, including the bindings that were
    // dropped while the rest of the profile bound fine.
    InputBindingCache& bindings = world.GetResource<InputBindingCache>();
    const InputActionRegistry* actions = bindings.GetActions(profile);
    if (actions == nullptr)
    {
        Log.Error("TemplateGame: input profile did not bind: {}",
                  DescribeBindErrors(bindings.Status(profile)));
        return;
    }

    TemplateInputActions& ids = world.HasResource<TemplateInputActions>()
        ? world.GetResource<TemplateInputActions>()
        : world.AddResource<TemplateInputActions>();
    ids.Move = actions->Find("move");
    ids.Look = actions->Find("look");
    ids.Jump = actions->Find("jump");

    LookInputBinding& look = world.HasResource<LookInputBinding>()
        ? world.GetResource<LookInputBinding>()
        : world.AddResource<LookInputBinding>();
    look.Look = ids.Look;

    GameplayInput = world.GetResource<InputContextSet>().Activate("gameplay");
}
