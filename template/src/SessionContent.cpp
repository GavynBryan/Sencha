#include "SessionContent.h"

#include "GameSettingsData.h"
#include "PawnSpawn.h"
#include "PawnStreaming.h"
#include "FpsInputActions.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookOrientation.h>
#include <core/assets/AssetLease.h>
#include <core/config/EngineConfig.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <world/RuntimeWorld.h>

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

    // The world stays loaded around this machine's player, and the player
    // stays out of rooms that are not ready. Both are this game's decisions.
    PawnStreaming& streaming = ctx.Schedule.Register<PawnStreaming>();
    streaming.Owner = &Host;
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        streaming.Movers = &step->GetCharacterMovers();
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

    FpsInputActions& ids = world.HasResource<FpsInputActions>()
        ? world.GetResource<FpsInputActions>()
        : world.AddResource<FpsInputActions>();
    ids.Move = actions->Find("move");
    ids.Look = actions->Find("look");
    ids.Jump = actions->Find("jump");

    LookInputBinding& look = world.HasResource<LookInputBinding>()
        ? world.GetResource<LookInputBinding>()
        : world.AddResource<LookInputBinding>();
    look.Look = ids.Look;

    GameplayInput = world.GetResource<InputContextSet>().Activate("gameplay");
}
