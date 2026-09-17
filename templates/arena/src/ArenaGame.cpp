#include "ArenaGame.h"

#include "ArenaSteeringSystem.h"
#include "PawnCameraSystem.h"
#include "PawnSpawn.h"
#include "samples/turret/TurretSample.h"

#include <abilities/AbilityKit.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/GameModule.h>
#include <controller/ControllerRegistration.h>
#include <controller/LookIntegrationSystem.h>
#include <core/config/EngineConfig.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <net/PeerCommandRuntime.h>
#include <participant/ParticipantLifecycle.h>
#include <physics/PhysicsRegistration.h>
#include <world/RuntimeWorld.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>

ArenaSessionPolicy& ArenaGame::Session()
{
    assert(SessionState.has_value() && "OnStart composes the session before anything asks");
    return *SessionState;
}

void ArenaGame::OnConfigure(GameConfigureContext& ctx)
{
    // The name is the game's identity to the host -- its window, its saved
    // settings' namespace -- so it is the game's to state, not the host's to
    // guess.
    ctx.Config.App.Name = "Sencha Arena Template";
}

void ArenaGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();

    // Gameplay owns the mouse while it is being played. A standing request,
    // stated once: the shell releases the pointer while a menu is up and takes
    // it back on resume, and the platform layer drops it on focus loss, so
    // nothing here has to notice any of that happening.
    engine.SetPointerCaptured(true);

    SessionState.emplace(engine, engine.Logging().GetLogger<ArenaGame>());
    SessionState->Open();

    // What a participant is in this game, and where its body comes from. The
    // engine runs the lifecycle -- admit, compose, ask for a body, bind it,
    // reap on departure -- and keeps the book on a prefab request from the ask
    // until its group is handed over or cleaned up. What only this game can
    // answer is which prefab and where it stands.
    Logger& log = engine.Logging().GetLogger<ArenaGame>();
    Bodies.emplace(
        engine.World().Entities(), engine.Spawns(), log,
        [this, &log](const World& world, EntityId participant) {
            return ChoosePawnSpawn(world, participant, Session().GameSettings(), log);
        },
        [&engine](EntityId participant) {
            return engine.RequestParticipantBody(participant);
        },
        [&log](World& world, EntityId participant, EntityId root) {
            PreparePawn(world, participant, root, log);
        });
    engine.Participants().ProvideBody =
        [this](World&, EntityId participant) { return Bodies->ProvideBody(participant); };
    // A prefab body is a group: the engine reaps the root like any body, and
    // the queued despawn sweeps the group's remaining members at the next
    // pump -- without it, prefab children would outlive the pawn outside any
    // group index.
    engine.Participants().ReapBody =
        [this](World&, EntityId, EntityId body) {
            (void)Bodies->RequestDespawnBody(body);
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

    // Movement stands on the ability kit: a pawn's move speed is an attribute
    // the kit resolves each tick, and the movement pipeline orders itself
    // against the kit's systems. The kit comes first by that contract.
    RegisterAbilityKitSystems(ctx.Schedule);
    RegisterMovementSystems(ctx.Schedule, Session().Assets().DataAssets,
                            &GetEngine().Logging());
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
        settlement.Bodies = &*Bodies;
        ctx.Schedule.After<SessionPlayerSystem, SpawnSettlementSystem>();
    }
}

void ArenaGame::OnShutdown(GameShutdownContext&)
{
    GetEngine().SetPointerCaptured(false);
    // The lifecycle's answers go first, so nothing asks a closed book. The
    // closed book itself stays put: the level's final detaches still reach the
    // system that points at it, and a closed book touches nothing.
    GetEngine().Participants().ProvideBody = {};
    GetEngine().Participants().ReapBody = {};
    if (Bodies.has_value())
        Bodies->Close();
    if (SessionState.has_value())
        SessionState->Close();
    SessionState.reset();
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

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static ArenaGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
