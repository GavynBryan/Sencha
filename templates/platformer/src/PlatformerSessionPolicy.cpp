#include "PlatformerSessionPolicy.h"

#include "PlatformerSettingsData.h"
#include "PawnSpawn.h"
#include "PlatformerInputActions.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <components/ActiveCameraService.h>
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
constexpr std::string_view kInputProfilePath =
    "asset://data/input_default.sdata";
constexpr std::string_view kGameSettingsPath = "asset://data/game.sdata";


} // namespace

void RegisterPlatformerDataTypes(DataAssetTypeRegistry& types,
                               DataSchemaRegistry& schemas)
{
    RegisterPlatformerSettingsData(types, schemas);
}

void UnregisterPlatformerDataTypes(DataAssetTypeRegistry& types,
                                 DataSchemaRegistry& schemas)
{
    UnregisterPlatformerSettingsData(types, schemas);
}

PlatformerSessionPolicy::PlatformerSessionPolicy(Engine& engine, Logger& log)
    : Host(engine)
    , Log(log)
{
}

PlatformerSessionPolicy::~PlatformerSessionPolicy() = default;

void PlatformerSessionPolicy::Open()
{
    Engine& engine = Host;
    RuntimeAssets& runtimeAssets = Assets();

    // Which gameplay features exist in this game's world. The engine's schema
    // registry carries the vocabulary for every component cooked content can
    // name; this decides which of them get storage here.
    World& world = engine.World().Entities();
    RegisterPhysicsComponents(world);
    RegisterMovement(world);

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

void PlatformerSessionPolicy::Close()
{
    World& world = Host.World().Entities();

    // Presentation policy: what this game was looking through stops being the
    // view before the level it was looking at goes.
    world.GetResource<ActiveCameraService>().SetActive(EntityId{});

    // Same rule the engine's content teardown follows, for the things this game
    // holds: the context lease and every data-asset handle go here, while their
    // owners still exist. The game object is a module-static whose destructor
    // runs at dlclose, long after the world that owns the context set.
    Input.Reset();
    GameSettingsAsset.Reset();
}

void PlatformerSessionPolicy::RegisterSystems(SystemRegisterContext& ctx)
{
    // Where a level's collision goes. Physics is this game's choice, so the
    // engine cannot know the cache exists until the schedule is composed.
    if (PhysicsStepSystem* step = ctx.Schedule.Get<PhysicsStepSystem>())
        Host.Level().ConnectCollision(step->GetShapeCache());

}


RuntimeAssets& PlatformerSessionPolicy::Assets()
{
    return Host.Content().Assets();
}

// Loads one structured data asset synchronously and returns an owned lease.
// Returns an invalid handle on any failure, which every caller treats as
// "run without the authored data" rather than as a fatal error.
DataAssetCacheHandle PlatformerSessionPolicy::AcquireDataAsset(std::string_view path)
{
    AssetLease lease = Assets().Assets.LoadLease(path, AssetType::Data);
    if (!lease.IsValid())
    {
        Log.Warn("PlatformerGame: '{}' did not load; running without it", path);
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
const CompiledPlatformerSettings* PlatformerSessionPolicy::GameSettings()
{
    if (!GameSettingsAsset.IsValid())
        GameSettingsAsset = AcquireDataAsset(kGameSettingsPath);
    if (!GameSettingsAsset.IsValid())
        return nullptr;
    const CompiledPlatformerSettings* settings =
        Assets().DataAssets.TryGet<CompiledPlatformerSettings>(
            GameSettingsAsset.GetToken(), "game.settings");
    if (settings == nullptr)
        Log.Warn("PlatformerGame: '{}' is not a game.settings", kGameSettingsPath);
    return settings;
}

// Binds the game's controls. The action set loads first: a profile names its
// actions, and binding cannot resolve those names until the set is resident.
void PlatformerSessionPolicy::SetupInputMapping()
{
    World& world = Host.World().Entities();
    Input = BindInputProfile(world, Assets(), kInputProfilePath, "gameplay", Log);
    if (!Input.Ready())
    {
        Log.Error("PlatformerGame: no controls; the game cannot be played");
        return;
    }

    PlatformerInputActions& actions = world.HasResource<PlatformerInputActions>()
        ? world.GetResource<PlatformerInputActions>()
        : world.AddResource<PlatformerInputActions>();
    actions.Move = Input.Require("move", Log);
    actions.Look = Input.Require("look", Log);
    actions.Jump = Input.Require("jump", Log);
}
