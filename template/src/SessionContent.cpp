#include "SessionContent.h"

#include "GameSettingsData.h"
#include "PawnSpawn.h"
#include "TemplateInputActions.h"

#include <anim/AnimationClipPlaybackRuntime.h>
#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <audio/AudioSourceRuntime.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <controller/ControllerRegistration.h>
#include <assets/runtime/ContentMount.h>
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

} // namespace

void RegisterTemplateDataTypes(DataAssetTypeRegistry& types,
                               DataSchemaRegistry& schemas)
{
    RegisterGameSettingsData(types, schemas);
}

void UnregisterTemplateDataTypes(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas)
{
    UnregisterGameSettingsData(types, schemas);
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
    RuntimeAssets& runtimeAssets = Assets();

    // Which gameplay features exist in this game's world. The engine's schema
    // registry carries the vocabulary for every component cooked content can
    // name; this decides which of them get storage here.
    World& world = engine.World().Entities();
    RegisterPhysicsComponents(world);
    RegisterMovement(world);
    RegisterCameraComponents(world);
    RegisterControllerComponents(world);

    SetupInputMapping();
    ZoneLoader.emplace(
        engine.Tasks(),
        engine.World(),
        engine.RuntimeComponents(),
        engine.SceneSerializers(),
        engine.Content().SceneContext(),
        engine.Runtime());
    Preloader.emplace(
        engine.Logging(),
        runtimeAssets.Registry,
        runtimeAssets.Assets,
        engine.Tasks());

#ifdef SENCHA_ENABLE_DEBUG_UI
    // The other half of the movement tuning loop: the editor predicts what a
    // profile does, this reports what the running game resolved from it.
    // Composed here rather than by the engine overlay because the world being
    // simulated is the game's.
    engine.AddDebugPanel(std::make_unique<MovementStatePanel>(
        world, &runtimeAssets.DataAssets));
#endif
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

    PlayZoneActive = false;
    // Before the runtime it points at goes.
    engine.SetWorldStreaming(nullptr, nullptr);
    Partition.reset();
    ZoneLoader.reset();
    Preloader.reset();

    // Same rule the engine's content teardown follows, for the things this game
    // holds: the context lease and every data-asset handle go here, while their
    // owners still exist. The game object is a module-static whose destructor
    // runs at dlclose, long after the world that owns the context set.
    GameplayInput.Reset();
    InputActionSetAsset.Reset();
    InputProfileAsset.Reset();
    GameSettingsAsset.Reset();
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
                Host.Content().SceneContext(),
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
    return Host.Content().Assets();
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
