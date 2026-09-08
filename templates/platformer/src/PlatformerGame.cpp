#include "PlatformerGame.h"

#include "CameraRelativeSteeringSystem.h"
#include "OrbitCameraSystem.h"
#include "PawnSpawn.h"

#include <abilities/AbilityKit.h>
#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/GameModule.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputRegistration.h>
#include <movement/MovementRegistration.h>
#include <participant/ParticipantLifecycle.h>
#include <physics/PhysicsRegistration.h>
#include <world/RuntimeWorld.h>

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>

PlatformerSessionPolicy& PlatformerGame::Session()
{
    assert(SessionState.has_value() && "OnStart composes the session before anything asks");
    return *SessionState;
}

void PlatformerGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    SessionState.emplace(engine, engine.Logging().GetLogger<PlatformerGame>());
    SessionState->Open();

    // What a participant is in this game, and where its body comes from. The
    // engine runs the lifecycle -- admit, compose, ask for a body, bind it,
    // reap on departure -- and keeps the book on a prefab request from the ask
    // until its group is handed over or cleaned up. What only this game can
    // answer is which prefab and where it stands.
    Logger& log = engine.Logging().GetLogger<PlatformerGame>();
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

    // A dedicated host has nobody at a keyboard, so it is told how to serve
    // rather than how to play.
    std::printf("Sencha platformer template\n");
    std::printf("  Load a map: +map levels/platformer_room\n");
    std::printf("  Right mouse: orbit | WASD: move | Space: jump\n");
}

void PlatformerGame::OnRegisterSystems(SystemRegisterContext& ctx)
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
    RegisterInputSystems(
        ctx.Schedule,
        Session().Assets().DataAssets,
        GetEngine().Logging());
    ctx.Schedule.Register<CameraRelativeSteeringSystem>();

    // The character steers on the tick record, after actions resolve, and in
    // the camera's frame -- which the orbit system wrote last frame.
    ctx.Schedule.After<CameraRelativeSteeringSystem, InputActionResolveSystem>();
    OrderMovementAfterInput<CameraRelativeSteeringSystem>(ctx.Schedule);

    // Waits on content with no session, and on the authority with one: either
    // way its first act each frame is to ask where this player's pawn comes
    // from.
    {
        Logger& log = GetEngine().Logging().GetLogger<PlatformerGame>();
        OrbitCameraSystem& camera = ctx.Schedule.Register<OrbitCameraSystem>();
        camera.Log = &GetEngine().Logging().GetLogger<PlatformerGame>();
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

void PlatformerGame::OnPlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Handled)
        return;

    // Looking is holding the right button. Whether the pointer is actually
    // captured while it is held -- focus, the console -- is the engine's.
    if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        GetEngine().SetPointerCaptured(true);
    }
    else if (ctx.Event.type == SDL_EVENT_MOUSE_BUTTON_UP
             && ctx.Event.button.button == SDL_BUTTON_RIGHT)
    {
        GetEngine().SetPointerCaptured(false);
    }
}

void PlatformerGame::OnShutdown(GameShutdownContext&)
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
void PlatformerGame::OnRegisterDataAssetTypes(DataAssetTypeRegistry& types,
                                            DataSchemaRegistry& schemas)
{
    RegisterPlatformerDataTypes(types, schemas);
}

void PlatformerGame::OnUnregisterDataAssetTypes(DataAssetTypeRegistry& types,
                                              DataSchemaRegistry& schemas)
{
    UnregisterPlatformerDataTypes(types, schemas);
}

extern "C" SENCHA_GAME_EXPORT Game* SenchaCreateGameModule()
{
    static PlatformerGame instance;
    return &instance;
}

SENCHA_EXPORT_GAME_MODULE_ABI()
