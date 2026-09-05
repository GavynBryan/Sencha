#include "ArenaGame.h"

#include "ArenaSteeringSystem.h"
#include "PawnCameraSystem.h"
#include "PawnSpawn.h"

#include "ArenaSettingsData.h"
#include "ArenaStart.h"
#include "ArenaInputActions.h"
#include "samples/turret/TurretSample.h"

#include <abilities/AbilityKit.h>
#include <anim/AnimationClipPlaybackSystem.h>
#include <app/Engine.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleService.h>
#ifdef SENCHA_ENABLE_DEBUG_UI
#include <debug/MovementStatePanel.h>
#endif
#include <app/GameModule.h>
#include <camera/CameraRegistration.h>
#include <components/ActiveCameraService.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookIntegrationSystem.h>
#include <ecs/Query.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <math/Quat.h>
#include <math/geometry/3d/Transform3d.h>
#include <movement/MotionComposition.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionSource.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetSpawnPrefab.h>
#include <net/NetOwnership.h>
#include <net/NetSession.h>
#include <net/PeerCommandRuntime.h>
#include <participant/ParticipantLifecycle.h>
#include <participant/LocalControl.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/ZoneCollisionLoader.h>
#include <platform/PlatformServices.h>
#include <platform/SdlWindow.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/ZonePackageImporter.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

ArenaSessionPolicy& ArenaGame::Session()
{
    assert(Content.has_value() && "OnStart composes the session before anything asks");
    return *Content;
}

void ArenaGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    Content.emplace(engine, engine.Logging().GetLogger<ArenaGame>());
    Content->Open();

    // What a participant is in this game, and where its body comes from. The
    // engine runs the lifecycle -- admit, compose, ask for a body, bind it,
    // reap on departure -- and these answer the two questions only the game
    // can. A peer loop and an orphan sweep used to live here instead.
    engine.Participants().ProvideBody =
        [this](World& world, EntityId participant) -> EntityId
    {
        // Nowhere to put a body until content has loaded. Returning none is an
        // ordinary answer, and the engine does not ask again on its own -- the
        // map load asks, once it has somewhere to put one.
        if (world.TryGetResource<PlayContentPartition>() == nullptr)
            return EntityId{};

        Logger& log = GetEngine().Logging().GetLogger<ArenaGame>();
        const NetParticipantIdentity* who =
            world.TryGet<NetParticipantIdentity>(participant);
        const std::uint32_t peer = who == nullptr ? 0u : who->Peer;

        const auto spawnPosition = [&]() -> Vec3d
        {
            // Unfiltered: a map's content is imported into its own zone
            // partition, so a start looked for only in the persistent one is a
            // start that is never found and a peer that arrives at the origin.
            const std::optional<Vec3d> authored =
                FindPlayerStart(world, std::nullopt);
            // Said out loud once per spawn, because everything downstream of
            // it looks exactly like a level that authored a start at the
            // origin -- including anything else near where a player begins.
            if (!authored.has_value())
            {
                log.Warn("ArenaGame: no player_start in the loaded content; "
                         "spawning at the default position");
            }
            // Offset laterally from the start so two players do not arrive
            // inside each other, by peer id so somebody lands in the same
            // place however many others are present. A proper multi-start
            // rotation is the level's business, not this policy's.
            Vec3d spawn = authored.value_or(kDefaultPlayerStart);
            spawn.X += 2.0f * static_cast<float>(peer);
            return spawn;
        };

        // Named rather than numbered for the one with no peer behind it. Peer
        // zero is the authority, so "a pawn for peer 0" describes the person
        // at this machine as a connection that does not exist.
        const auto announce = [&](std::string_view how)
        {
            if (peer == kNetAuthorityPeer)
                log.Info("ArenaGame: spawned a pawn for the player at "
                         "this machine ({})", how);
            else
                log.Info("ArenaGame: spawned a pawn for peer {} ({})", peer,
                         how);
        };

        // No prefab is no body. Said at Error rather than papered over with a
        // built-in one: a player driving a diagnostic capsule while the game
        // believes it is running is exactly what must not pass unremarked,
        // and a game with no pawn content is a game that is not set up yet.
        const CompiledArenaSettings* settings = Session().GameSettings();
        if (settings == nullptr || settings->PlayerPawnScenePath.empty())
        {
            log.Error("ArenaGame: no player pawn prefab configured "
                      "(game.settings player_pawn); nobody gets a body");
            return EntityId{};
        }

        // The prefab path is asynchronous: the first ask requests the spawn
        // and answers "not yet"; the settlement system asks again when the
        // request settles, and this branch then consumes it.
        PendingSceneSpawns& pending = PendingSpawnsOf(world);
        const auto entry = std::find_if(
            pending.Pawns.begin(), pending.Pawns.end(),
            [&](const PendingSceneSpawns::PawnRequest& request)
            { return request.Participant == participant; });
        if (entry == pending.Pawns.end())
        {
            Transform3f root = Transform3f::Identity();
            root.Position = spawnPosition();
            const SceneSpawnId id = GetEngine().Spawns().RequestSpawn(
                settings->PlayerPawnScenePath, root, PersistentStoragePartition);
            pending.Pawns.push_back({ participant, id });
            return EntityId{};
        }

        switch (GetEngine().Spawns().Status(entry->Spawn))
        {
        case SceneSpawnStatus::Pending:
            return EntityId{};
        case SceneSpawnStatus::Live:
        {
            const EntityId root = SpawnedGroupRoot(
                world, GetEngine().Spawns().Entities(entry->Spawn));
            if (!root.IsValid())
            {
                // The group's partition unloaded underneath the request; a
                // fresh ask starts over against the current content.
                pending.Pawns.erase(entry);
                return EntityId{};
            }
            // The prefab is the pawn: its mesh, controller, tuning, mode,
            // aim, tags, attributes, and abilities are all authored, and the
            // per-tick columns come with the movement component.
            StampNetPrefab(world, root, log);
            pending.LiveBodies.emplace_back(participant, entry->Spawn);
            pending.Pawns.erase(entry);
            announce("pawn prefab");
            return root;
        }
        case SceneSpawnStatus::Failed:
        default:
            log.Error("ArenaGame: pawn prefab '{}' failed to spawn; nobody "
                      "gets a body", settings->PlayerPawnScenePath);
            pending.Pawns.erase(entry);
            return EntityId{};
        }
    };

    // A prefab body is a group: the engine reaps the root like any body, and
    // the queued despawn sweeps the group's remaining members at the next
    // pump -- without it, prefab children would outlive the pawn outside any
    // group index. Procedural bodies take only the engine-side destroy.
    engine.Participants().ReapBody =
        [this](World& world, EntityId participant, EntityId) -> bool
    {
        PendingSceneSpawns* pending = world.TryGetResource<PendingSceneSpawns>();
        if (pending == nullptr)
            return true;
        const auto live = std::find_if(
            pending->LiveBodies.begin(), pending->LiveBodies.end(),
            [&](const auto& body) { return body.first == participant; });
        if (live == pending->LiveBodies.end())
            return true;
        (void)GetEngine().Spawns().RequestDespawn(live->second);
        pending->LiveBodies.erase(live);
        return true;
    };

    InstallTurretSample(engine, Session());


    // A dedicated host has nobody at a keyboard, so it is told how to serve
    // rather than how to play.
    std::printf("Sencha arena template\n");
    std::printf("  Load a map: +map levels/arena_room\n");
    if (GetEngine().Config().Runtime.HasLocalPlayer)
        std::printf("  Right mouse: look | WASD: move | Space: jump\n");
    else
        std::printf("  Host a session: +host [port] | see net_status, net_zones\n");
}

void ArenaGame::OnRegisterSystems(SystemRegisterContext& ctx)
{
    RegisterPhysics(ctx.Schedule);
    // After physics, which owns the shape cache the loaded content's collision
    // goes into and the movers the streaming correction writes through.
    Session().RegisterSystems(ctx);

    RegisterAbilityKitSystems(ctx.Schedule);
    // Clip playback advances animation time on the fixed tick; the render
    // extract samples whatever time it leaves behind.
    RegisterAnimationSystems(ctx.Schedule);
    RegisterMovementSystems(ctx.Schedule, Session().Assets().DataAssets,
                            &GetEngine().Logging());
    RegisterInputSystems(
        ctx.Schedule,
        Session().Assets().DataAssets,
        GetEngine().Logging());
    RegisterControllerSystems(ctx.Schedule);
    RegisterNetSystems(ctx.Schedule, GetEngine().PeerCommands(),
                       GetEngine().Prediction(), GetEngine().Interpolation(),
                       GetEngine().NetClock());
    ctx.Schedule.Register<ArenaSteeringSystem>();

    // Everything that reads actions runs after they are resolved: the aim
    // integrates on the frame snapshot, the character steers on the tick record
    // along the orientation that produced.
    ctx.Schedule.After<LookIntegrationSystem, InputActionResolveSystem>();
    ctx.Schedule.After<ArenaSteeringSystem, LookIntegrationSystem>();
    ctx.Schedule.After<ArenaSteeringSystem, InputActionResolveSystem>();
    // The two edges the net input channel needs around whichever system turns
    // actions into intent. Declared by the engine, which owns why they exist.
    OrderNetInputAround<ArenaSteeringSystem>(ctx.Schedule);
    OrderMovementAfterInput<ArenaSteeringSystem>(ctx.Schedule);
    RegisterTurretSampleSystems(GetEngine(), ctx.Schedule);

    // Waits on content with no session, and on the authority with one: either
    // way its first act each frame is to ask where this player's pawn comes
    // from.
    {
        Logger& log = GetEngine().Logging().GetLogger<ArenaGame>();
        PawnCameraSystem& camera = ctx.Schedule.Register<PawnCameraSystem>();
        camera.Owner = &GetEngine();
        camera.Log = &GetEngine().Logging().GetLogger<ArenaGame>();
        SessionPlayerSystem& players = ctx.Schedule.Register<SessionPlayerSystem>();
        players.Owner = &GetEngine();
        players.Log = &log;

        // Settles pending scene spawns before the session presents bodies, so
        // a pawn that lands this frame is followed this frame.
        SpawnSettlementSystem& settlement =
            ctx.Schedule.Register<SpawnSettlementSystem>();
        settlement.Owner = &GetEngine();
        settlement.Log = &log;
        ctx.Schedule.After<SessionPlayerSystem, SpawnSettlementSystem>();
    }
}

void ArenaGame::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Handled)
        return;

    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(true);
    }
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
             && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        SetRelativeMouseMode(false);
    }
    else if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        SetRelativeMouseMode(false);
    }
}

void ArenaGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(false);
    if (Content.has_value())
        Content->Close();
    Content.reset();
}

// This game's data vocabulary, registered into whichever registries are asking:
// the engine's content stack at startup, and the data editor's.
void ArenaGame::OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                            DataSchemaRegistry& schemas)
{
    RegisterArenaDataTypes(types, schemas);
}

void ArenaGame::OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                              DataSchemaRegistry& schemas)
{
    UnregisterArenaDataTypes(types, schemas);
}

void ArenaGame::SetRelativeMouseMode(bool enabled)
{
    // No window to capture a pointer into on a headless host.
    PlatformServices* platform = GetEngine().TryPlatform();
    if (platform == nullptr)
        return;

    SdlWindow* window = platform->Windows.GetPrimaryWindow();
    if (window == nullptr || window->GetHandle() == nullptr)
        return;
    if (SDL_GetWindowRelativeMouseMode(window->GetHandle()) == enabled)
        return;
    SDL_SetWindowRelativeMouseMode(window->GetHandle(), enabled);
}

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static ArenaGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
