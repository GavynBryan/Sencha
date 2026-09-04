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
constexpr std::string_view kInputActionSetPath =
    "asset://data/input_actions.sdata";
constexpr std::string_view kInputProfilePath =
    "asset://data/input_default.sdata";
constexpr std::string_view kGameSettingsPath = "asset://data/game.sdata";


struct WorldPartitionUpdateSystem
{
    explicit WorldPartitionUpdateSystem(Engine& engine)
        : Host(engine)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        const WorldPartitionRuntime* partition = Host.WorldStreaming();
        if (partition == nullptr || !partition->HasManifest())
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
            && partition->LastTraversal().Status
                == DockTraversalStatus::BlockedDestinationNotReady)
        {
            PendingSafePosition = partition->LastTraversal().SafeSourcePosition;
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

    Engine& Host;
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
    World& world = Host.World().Entities();

    // Presentation policy: what this game was looking through stops being the
    // view before the level it was looking at goes.
    world.GetResource<ActiveCameraService>().SetActive(EntityId{});

    // Same rule the engine's content teardown follows, for the things this game
    // holds: the context lease and every data-asset handle go here, while their
    // owners still exist. The game object is a module-static whose destructor
    // runs at dlclose, long after the world that owns the context set.
    GameplayInput.Reset();
    InputActionSetAsset.Reset();
    InputProfileAsset.Reset();
    GameSettingsAsset.Reset();
}

void SessionContent::RegisterSystems(SystemRegisterContext& ctx)
{
    // Where a level's collision goes. Physics is this game's choice, so the
    // engine cannot know the cache exists until the schedule is composed.
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        Host.Level().ConnectCollision(step->GetShapeCache());

    WorldPartitionUpdateSystem& partitionUpdate =
        ctx.Schedule.Register<WorldPartitionUpdateSystem>(Host);
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        partitionUpdate.Movers = &step->GetCharacterMovers();
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
