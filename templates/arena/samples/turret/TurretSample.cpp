#include "samples/turret/TurretSample.h"

#include "src/ArenaSessionPolicy.h"
#include "src/PawnSpawn.h"
#include "samples/turret/TurretControl.h"
#include "samples/turret/TurretMount.h"

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <controller/LookIntegrationSystem.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSession.h>
#include <runtime/spawn/SceneSpawnService.h>

#include <span>
#include <string>

namespace
{
// The client's half of taking a turret, and the whole of what a game has to
// write to address a networked object: find the one you mean, ask replication
// what it is called, and send that. A local EntityId means nothing on another
// machine; the identity map turns "this thing in front of me" into something
// both machines agree about, and refuses to name anything replication did not
// hand this machine.
ConsoleResult RequestTurret(Engine& engine, ArenaSessionPolicy& session, bool placeOnly)
{
    ConsoleResult result;
    if (!engine.World().Entities().IsRegistered<TurretMount>())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("this build has no turrets");
        return result;
    }

    // A client decides nothing about who drives what, or about what exists, so
    // it asks. Anywhere else this process is the authority that request would
    // have been sent to, and the same rules answer it without one.
    NetSession* net = engine.TryNet();
    const bool client = net != nullptr && net->Role() == NetSessionRole::Client;
    Logger& log = engine.Logging().GetLogger<TurretMount>();
    if (placeOnly)
    {
        if (client)
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error("only the authority places turrets");
            return result;
        }
        return PlaceTurretHere(engine, session.GameSettings(), log);
    }
    if (client)
        return AskAuthorityForTurret(engine, *net);
    return TakeTurretHere(engine, session.GameSettings(), log);
}

// A placed turret's spawn landing: it gets its runtime body, its replication
// marker (a turret exists to be possessed over the wire), and its waiting
// driver.
struct TurretSettlementSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        World& world = ctx.Entities;
        PendingTurretSpawns* pending = world.TryGetResource<PendingTurretSpawns>();
        if (pending == nullptr)
            return;
        SceneSpawnService& spawns = Owner->Spawns();

        for (std::size_t i = 0; i < pending->Turrets.size();)
        {
            const SceneSpawnId id = pending->Turrets[i].Spawn;
            if (spawns.Status(id) == SceneSpawnStatus::Pending)
            {
                ++i;
                continue;
            }
            const EntityId possessor = pending->Turrets[i].Possessor;
            pending->Turrets[i] = pending->Turrets.back();
            pending->Turrets.pop_back();

            if (spawns.Status(id) != SceneSpawnStatus::Live)
            {
                Log->Warn("ArenaGame: the turret prefab failed to spawn");
                continue;
            }
            const EntityId root = SpawnedGroupRoot(world, spawns.Entities(id));
            if (!root.IsValid() || world.TryGet<TurretMount>(root) == nullptr)
            {
                Log->Error("ArenaGame: the turret prefab's root carries no "
                           "turret_mount; dropping the placement");
                (void)spawns.RequestDespawn(id);
                continue;
            }
            world.AddComponent<NetReplicated>(root);
            StampNetPrefab(world, root, *Log);
            Log->Info("ArenaGame: placed a turret");
            if (possessor.IsValid() && world.IsAlive(possessor))
                (void)ApplyTurretRequest(*Owner, world, possessor, root, PeerId{});
        }
    }
};
} // namespace

void InstallTurretSample(Engine& engine, ArenaSessionPolicy& session)
{
    Logger& log = engine.Logging().GetLogger<TurretMount>();

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
                    authority, authority.Logging().GetLogger<TurretMount>(), message);
            },
            &engine))
    {
        log.Error("ArenaGame: payload kind {} was already answered; turret "
                  "requests will not be handled",
                  static_cast<unsigned>(kTurretRequestKind));
    }

    engine.Console().Registry().RegisterCommand({
        .Name = "turret",
        .Owner = "game",
        .Usage = "turret [place]",
        .Help = "Take the nearest turret, or leave the one you are in; "
                "`turret place` puts one down without taking it.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine, &session](ConsoleExecutionContext&,
                                        std::span<const std::string> args) {
            if (args.size() > 1 || (args.size() == 1 && args[0] != "place"))
            {
                ConsoleResult usage;
                usage.Status = ConsoleStatus::InvalidArguments;
                usage.Error("usage: turret [place]");
                return usage;
            }
            return RequestTurret(engine, session, !args.empty());
        },
    });
}

void RegisterTurretSampleSystems(Engine& engine, EngineSchedule& schedule)
{
    TurretSettlementSystem& settlement = schedule.Register<TurretSettlementSystem>();
    settlement.Owner = &engine;
    settlement.Log = &engine.Logging().GetLogger<TurretMount>();
    // A turret points where its driver looks. After the look integrates, for
    // the same reason the character steers after it: the value it reads is
    // this tick's aim rather than last tick's.
    schedule.Register<TurretAimSystem>();
    schedule.After<TurretAimSystem, LookIntegrationSystem>();
}
