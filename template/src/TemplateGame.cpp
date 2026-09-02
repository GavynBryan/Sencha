#include "TemplateGame.h"

#include "CharacterInputSystem.h"
#include "ObserverFlight.h"
#include "PawnSpawn.h"

#include "GameSettingsData.h"
#include "PlayerStartComponent.h"
#include "TemplateInputActions.h"
#include "SpinComponent.h"
#include "TurretControl.h"
#include "TurretMount.h"

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
#include <camera/CameraRig.h>
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

namespace
{
struct SpinSystem
{
    void FixedLogic(FixedLogicContext& ctx)
    {
        World& world = ctx.Entities;
        if (!world.IsRegistered<SpinComponent>()
            || !world.IsRegistered<LocalTransform>())
        {
            return;
        }

        const float dt =
            static_cast<float>(ctx.Time.DeltaSeconds);
        Query<Write<SpinComponent>, Write<LocalTransform>> query(world);
        query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
        {
            auto spins = view.template Write<SpinComponent>();
            auto transforms = view.template Write<LocalTransform>();
            for (std::uint32_t index = 0;
                 index < view.Count();
                 ++index)
            {
                transforms[index].Value.Rotation =
                    transforms[index].Value.Rotation
                    * Quatf::FromAxisAngle(
                        Vec3d::Up(),
                        spins[index].RadiansPerSecond * dt);
            }
        });
    }
};
} // namespace

SessionContent& TemplateGame::Session()
{
    assert(Content.has_value() && "OnStart composes the session before anything asks");
    return *Content;
}

void TemplateGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    Content.emplace(engine, engine.Logging().GetLogger<TemplateGame>());
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

        Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
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
                log.Warn("TemplateGame: no player_start in the loaded content; "
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
                log.Info("TemplateGame: spawned a pawn for the player at "
                         "this machine ({})", how);
            else
                log.Info("TemplateGame: spawned a pawn for peer {} ({})", peer,
                         how);
        };

        // What this game hands a player when the pawn it wanted could not be
        // built. Said at Warn because a player flying a diagnostic body while
        // the game believes it is running is exactly what must not pass
        // unremarked.
        const auto observerPawn = [&]() -> EntityId
        {
            log.Warn("TemplateGame: no player pawn prefab; the player gets the "
                     "built-in observer body, which flies and has no content");
            const EntityId pawn = SpawnObserverPawn(world, spawnPosition());
            announce("observer");
            return pawn;
        };

        const CompiledGameSettings* settings = Session().GameSettings();
        if (settings == nullptr || settings->PlayerPawnScenePath.empty())
            return observerPawn();

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
            // The prefab is the pawn: its controller, tuning, mode, aim, tags,
            // attributes, and abilities are all authored, and the per-tick
            // columns come with the movement component. Only the mesh is still
            // code's to supply.
            AttachAvatarMesh(world, root, Session().PlayerAvatar());
            StampNetPrefab(world, root, log);
            pending.LiveBodies.emplace_back(participant, entry->Spawn);
            pending.Pawns.erase(entry);
            announce("pawn prefab");
            return root;
        }
        case SceneSpawnStatus::Failed:
        default:
            log.Warn("TemplateGame: pawn prefab '{}' failed to spawn; using "
                     "the built-in pawn", settings->PlayerPawnScenePath);
            pending.Pawns.erase(entry);
            return observerPawn();
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

    // Where a client's turret request is answered. One kind, one direction, one
    // handler -- and the direction is checked before the handler is reached, so
    // a client sending itself an authority-to-client kind is refused by the
    // router rather than by every handler having to think about it.
    if (!engine.NetMessages().Bind(
            kTurretRequestKind, NetMessageDirection::ClientToAuthority,
            [](void* context, const NetMessageContext& message)
            {
                Engine& authority = *static_cast<Engine*>(context);
                return AnswerTurretRequest(
                    authority,
                    authority.Logging().GetLogger<TemplateGame>(),
                    message);
            },
            &engine))
    {
        engine.Logging().GetLogger<TemplateGame>().Error(
            "TemplateGame: payload kind {} was already answered; turret "
            "requests will not be handled",
            static_cast<unsigned>(kTurretRequestKind));
    }

    engine.Console().SetMapHandler(
        [this](std::string_view mapName)
        {
            return Session().LoadMap(mapName);
        });

    engine.Console().Registry().RegisterCommand({
        .Name = "world",
        .Owner = "game",
        .Usage = "world <name>",
        .Help = "Load a cooked partitioned world and stream its zones around the player.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: world <name>");
                return usage;
            }
            return Session().LoadWorld(args[0]);
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "scene.spawn",
        .Owner = "game",
        .Usage = "scene.spawn <asset://...smap> [x y z]",
        .Help = "Spawn a cooked scene at the given position (origin by default).",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1 && args.size() != 4)
            {
                result.Error("usage: scene.spawn <asset://...smap> [x y z]");
                return result;
            }
            Transform3f root = Transform3f::Identity();
            if (args.size() == 4)
            {
                try
                {
                    root.Position = Vec3d(std::stof(args[1]), std::stof(args[2]),
                                          std::stof(args[3]));
                }
                catch (const std::exception&)
                {
                    result.Error("scene.spawn: position must be three numbers");
                    return result;
                }
            }
            const SceneSpawnId id =
                GetEngine().Spawns().RequestSpawn(args[0], root);
            result.Info("spawn " + std::to_string(id.Value) + " requested ("
                        + SceneSpawnStatusName(GetEngine().Spawns().Status(id))
                        + ")");
            return result;
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "scene.despawn",
        .Owner = "game",
        .Usage = "scene.despawn <spawn id>",
        .Help = "Destroy a live scene spawn's entities.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() != 1)
            {
                result.Error("usage: scene.despawn <spawn id>");
                return result;
            }
            SceneSpawnId id{};
            try
            {
                id.Value = std::stoull(args[0]);
            }
            catch (const std::exception&)
            {
                result.Error("scene.despawn: id must be a number");
                return result;
            }
            if (GetEngine().Spawns().RequestDespawn(id))
                result.Info("despawn queued");
            else
                result.Error("spawn " + std::to_string(id.Value) + " is "
                             + SceneSpawnStatusName(
                                 GetEngine().Spawns().Status(id)));
            return result;
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "turret",
        .Owner = "game",
        .Usage = "turret [place]",
        .Help = "Take the nearest turret, or leave the one you are in; "
                "`turret place` puts one down without taking it.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string> args) {
            if (args.size() > 1 || (args.size() == 1 && args[0] != "place"))
            {
                ConsoleResult usage;
                usage.Status = ConsoleStatus::InvalidArguments;
                usage.Error("usage: turret [place]");
                return usage;
            }
            return RequestTurret(!args.empty());
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "camera_mode",
        .Owner = "game",
        .Usage = "camera_mode <first|third|fixed>",
        .Help = "Switch the active camera between first-person, third-person, and the authored pose.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: camera_mode <first|third|fixed>");
                return usage;
            }
            return SetCameraMode(args[0]);
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "zone",
        .Owner = "game",
        .Usage = "zone <16-hex zone id>",
        .Help = "Focus the loaded world on a zone.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](
            ConsoleExecutionContext&,
            std::span<const std::string> args)
        {
            if (args.size() != 1)
            {
                ConsoleResult usage;
                usage.Error("usage: zone <16-hex zone id>");
                return usage;
            }
            return Session().FocusZone(args[0]);
        },
    });

    engine.Console().Registry().RegisterCommand({
        .Name = "zones",
        .Owner = "game",
        .Usage = "zones",
        .Help = "Print world partition demand and residency.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [this](ConsoleExecutionContext&,
                           std::span<const std::string>)
        {
            return Session().DescribeZones();
        },
    });

    // A dedicated host has nobody at a keyboard, so it is told how to serve
    // rather than how to play.
    std::printf("Sencha game template\n");
    std::printf("  Load a map: +map levels/<name>\n");
    std::printf("  Load a world: +world <name>\n");
    if (GetEngine().Config().Runtime.HasLocalPlayer)
        std::printf("  Right mouse: look | WASD: move | Space: jump\n");
    else
        std::printf("  Host a session: +host [port] | see net_status, net_zones\n");
}

// The client's half of taking a turret, and the whole of what a game has to
// write to address a networked object: find the one you mean, ask replication
// what it is called, and send that.
//
// Naming it is the part that could not be written before. A local EntityId is
// an index into one World and means nothing on another machine, so a request
// carrying one would be a request the authority could only guess at; the
// identity map is what turns "this thing in front of me" into something both
// machines agree about, and it refuses to name anything replication did not
// hand this machine -- so a client cannot invent an object and ask for it.
ConsoleResult TemplateGame::RequestTurret(bool placeOnly)
{
    ConsoleResult result;
    Engine& engine = GetEngine();

    if (!engine.World().Entities().IsRegistered<TurretMount>())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("this build has no turrets");
        return result;
    }

    // A client decides nothing about who drives what, or about what exists, so
    // it asks. Anywhere else -- a standalone game, and the player at a host's
    // own machine -- this process is the authority that request would have been
    // sent to, and the same rules answer it without one.
    NetSession* session = engine.TryNet();
    const bool client =
        session != nullptr && session->Role() == NetSessionRole::Client;

    Logger& log = engine.Logging().GetLogger<TemplateGame>();
    if (placeOnly)
    {
        if (client)
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error("only the authority places turrets");
            return result;
        }
        return PlaceTurretHere(engine, Session().GameSettings(), log);
    }

    if (client)
        return AskAuthorityForTurret(engine, *session);
    return TakeTurretHere(engine, Session().GameSettings(), log);
}

ConsoleResult TemplateGame::SetCameraMode(std::string_view modeName)
{
    ConsoleResult result;

    CameraRigMode mode{};
    if (modeName == "first")
        mode = CameraRigMode::FirstPerson;
    else if (modeName == "third")
        mode = CameraRigMode::ThirdPerson;
    else if (modeName == "fixed")
        mode = CameraRigMode::Fixed;
    else
    {
        result.Error("unknown camera mode '" + std::string(modeName)
                     + "'; expected first, third, or fixed");
        return result;
    }

    World& world = GetEngine().World().Entities();
    const EntityId camera =
        world.GetResource<ActiveCameraService>().GetActive();
    CameraRig* rig = camera.IsValid() ? world.TryGet<CameraRig>(camera) : nullptr;
    if (rig == nullptr)
    {
        result.Error("no active camera with a rig; load a map first");
        return result;
    }

    rig->Mode = mode;
    result.Info("camera mode " + std::string(modeName));
    return result;
}

void TemplateGame::OnRegisterSystems(SystemRegisterContext& ctx)
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
    RegisterCameraSystem(ctx.Schedule);
    RegisterControllerSystems(ctx.Schedule);
    RegisterNetSystems(ctx.Schedule, GetEngine().PeerCommands(),
                       GetEngine().Prediction(), GetEngine().Interpolation(),
                       GetEngine().NetClock());
    ctx.Schedule.Register<CharacterInputSystem>();

    // Everything that reads actions runs after they are resolved: the aim
    // integrates on the frame snapshot, the character steers on the tick record
    // along the orientation that produced.
    ctx.Schedule.After<LookIntegrationSystem, InputActionResolveSystem>();
    ctx.Schedule.After<CharacterInputSystem, LookIntegrationSystem>();
    ctx.Schedule.After<CharacterInputSystem, InputActionResolveSystem>();
    // The two edges the net input channel needs around whichever system turns
    // actions into intent. Declared by the engine, which owns why they exist.
    OrderNetInputAround<CharacterInputSystem>(ctx.Schedule);
    OrderMovementAfterInput<CharacterInputSystem>(ctx.Schedule);
    ctx.Schedule.Register<SpinSystem>();
    // A turret points where its driver looks. After the look integrates, for
    // the same reason the character steers after it: the value it reads is
    // this tick's aim rather than last tick's.
    ctx.Schedule.Register<TurretAimSystem>();
    ctx.Schedule.After<TurretAimSystem, LookIntegrationSystem>();

    // Waits on content with no session, and on the authority with one: either
    // way its first act each frame is to ask where this player's pawn comes
    // from.
    {
        Logger& log = GetEngine().Logging().GetLogger<TemplateGame>();
        SessionPlayerSystem& players = ctx.Schedule.Register<SessionPlayerSystem>();
        players.Owner = &GetEngine();
        players.Log = &log;
        // Resolves to no body on a process that cannot hold a mesh, which is
        // exactly what a bodyless pawn wants.
        players.Avatar = Session().PlayerAvatar();

        // Settles pending scene spawns before the session presents bodies, so
        // a pawn that lands this frame is followed this frame.
        SpawnSettlementSystem& settlement =
            ctx.Schedule.Register<SpawnSettlementSystem>();
        settlement.Owner = &GetEngine();
        settlement.Log = &log;
        ctx.Schedule.After<SessionPlayerSystem, SpawnSettlementSystem>();
    }
}

void TemplateGame::OnPlatformEvent(PlatformEventContext& ctx)
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

void TemplateGame::OnShutdown(GameShutdownContext&)
{
    SetRelativeMouseMode(false);
    if (Content.has_value())
        Content->Close();
    Content.reset();
}

// The same vocabulary the session registers into its own registries, aimed at
// the data editor's instead.
void TemplateGame::OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                            DataSchemaRegistry& schemas)
{
    RegisterTemplateDataTypes(types, schemas);
}

void TemplateGame::OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                              DataSchemaRegistry& schemas)
{
    UnregisterTemplateDataTypes(types, schemas);
}

void TemplateGame::SetRelativeMouseMode(bool enabled)
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
    static TemplateGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
