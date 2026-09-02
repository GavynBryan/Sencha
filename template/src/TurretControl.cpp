#include "TurretControl.h"

#include "GameSettingsData.h"
#include "PawnSpawn.h"
#include "TurretMount.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <controller/LookOrientation.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/NetParticipantIdentity.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
// Wide and low, so the one mesh this template ships reads as a mount rather
// than as a second player standing there. Scale is replicated state, so a
// client's copy arrives with these proportions instead of deriving them.
inline constexpr Vec3d kTurretProportions{ 1.2f, 0.4f, 1.2f };

// A turret, put somewhere because somebody asked for one. Placed rather than
// authored so the template ships the possession path without every level
// having to carry a turret; a real game would author them and this would go.
//
// Authority-only by construction: this is reached from answering a request,
// and a client answers none.
//
// The built-in stand-in for the turret prefab, so it carries by hand the two
// components the prefab authors: where its driver's aim lands, and the opt-in
// that turns the gun to it. Replicated with no prefab to name, so a peer
// receives its state and builds no body for it.
EntityId PlaceTurret(
    World& world,
    const Vec3d& at,
    StoragePartitionId partition)
{
    const EntityId turret =
        CreateTransformEntity(world, at, partition, kTurretProportions);
    world.AddComponent<TurretMount>(turret, TurretMount{});
    world.AddComponent<NetReplicated>(turret);
    world.AddComponent<LookOrientation>(turret, LookOrientation{});
    world.AddComponent<AimFacing>(turret);
    return turret;
}

// The body of a turret request: which turret, by the name replication gave it.
// Hand-written encode and decode against NetWriter/NetReader, which is the
// same idiom every untrusted decode in the engine is written in -- bounded,
// typed failure, nothing sized from what the sender claimed.
std::vector<std::byte> EncodeTurretRequest(NetEntityId target)
{
    std::array<std::byte, sizeof(std::uint64_t)> storage{};
    NetWriter writer(storage);
    writer.WriteU64(target.Value);
    const std::span<const std::byte> written = writer.Written();
    return std::vector<std::byte>(written.begin(), written.end());
}

bool DecodeTurretRequest(std::span<const std::byte> body, NetEntityId& out)
{
    NetReader reader(body);
    std::uint64_t value = 0;
    if (!reader.ReadU64(value) || !reader.AtEnd())
        return false;
    out = NetEntityId{ value };
    return out.IsValid();
}

// The nearest turret to a point, or none. Costs the number of turrets.
EntityId NearestTurret(const World& world, const Vec3d& from)
{
    if (!world.IsRegistered<TurretMount>())
        return EntityId{};

    EntityId nearest;
    float best = 0.0f;
    world.ForEachComponent<TurretMount>(
        [&](EntityId entity, const TurretMount&) {
            const LocalTransform* pose = world.TryGet<LocalTransform>(entity);
            if (pose == nullptr)
                return;
            const float distance = (pose->Value.Position - from).SqrMagnitude();
            if (!nearest.IsValid() || distance < best)
            {
                nearest = entity;
                best = distance;
            }
        });
    return nearest;
}

// Whoever is at this entity's controls, as a participant, or none.
//
// Asked of the participants rather than of ownership because those answer
// different questions: the player at the authority's own machine drives
// without owning anything, so ownership calls their turret free.
EntityId DriverOf(const World& world, EntityId subject)
{
    if (!subject.IsValid() || !world.IsRegistered<ParticipantControl>())
        return EntityId{};

    EntityId found;
    world.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl& held) {
            if (!found.IsValid() && held.ControlSubject == subject)
                found = participant;
        });
    return found;
}
} // namespace

//=============================================================================
// Answering a turret request
//
// One function for both directions and for whoever asked, because the question
// is the same one either way: what is this participant driving now, and what
// should it drive next. Everything it decides from is state the authority
// holds -- who drives what, and who owns what -- so a client asking and the
// player at this machine asking are answered by the same rules.
//
// Taking a turret parks the driver's pawn rather than leaving them owning
// both. One participant, one thing driven, which is what the engine's control
// slot says and is a design decision rather than a limitation worked around: a
// player at a fixed gun is not also running around.
//=============================================================================
TurretRequestOutcome ApplyTurretRequest(
    Engine& engine,
    World& world,
    EntityId participant,
    EntityId turret,
    PeerId driver)
{
    const ParticipantControl* control =
        participant.IsValid()
            ? world.TryGet<ParticipantControl>(participant)
            : nullptr;
    if (control == nullptr)
        return TurretRequestOutcome::Invalid;
    if (!turret.IsValid() || !world.IsAlive(turret)
        || !world.HasComponent<TurretMount>(turret))
    {
        return TurretRequestOutcome::Invalid;
    }

    if (control->ControlSubject == turret)
    {
        // Getting out. The gun goes back to the authority for the next person
        // to ask for, and their input returns to the body that never stopped
        // being theirs. Read before clearing ownership, which is structural
        // and moves the row this points into.
        const EntityId body = control->Body;
        NetClearOwner(world, turret);
        (void)engine.SetParticipantControlSubject(participant, body);
        return TurretRequestOutcome::Left;
    }

    // Somebody else is at the controls. Refused rather than taken: a request
    // that could evict its current driver is a request worth sending
    // constantly.
    if (DriverOf(world, turret).IsValid())
        return TurretRequestOutcome::Occupied;

    // Owned so its owner-only state reaches the driver, and driven so their
    // keys reach it. Two facts, not one: a gun somebody is at the controls of
    // is not necessarily a gun that belongs to them, and it is that difference
    // that leaves the gun standing when its driver quits.
    //
    // Their body keeps its owner through all of this. It is still theirs while
    // they are elsewhere, and only what they drive has moved.
    if (driver.IsValid())
        NetSetOwner(world, turret, driver);
    (void)engine.SetParticipantControlSubject(participant, turret);
    return TurretRequestOutcome::Took;
}

// What a client asked for, decided on the authority. The message carries one
// field -- which turret -- and every other fact is looked up here: who sent it
// comes from the session's own peer record, and what that names comes from the
// identity map replication minted. A message that could be believed about
// either would be a message a peer could use to drive somebody else's body.
bool AnswerTurretRequest(Engine& engine, Logger& log,
                        const NetMessageContext& message)
{
    World& world = message.Entities;

    NetEntityId named;
    if (!DecodeTurretRequest(message.Body, named))
        return false;
    if (message.Objects == nullptr)
        return false;

    // The one translation a client is allowed to ask for, and it fails closed:
    // an identity this authority never minted, or one it has released, names
    // nothing at all.
    const EntityId turret =
        message.Objects->AuthorityEntities().TryResolve(named);

    // Who is asking, as a participant rather than as a peer number. What they
    // drive and what their body is are both written down there.
    const EntityId participant = NetParticipantForPeer(world, message.From);

    switch (ApplyTurretRequest(engine, world, participant, turret, message.From))
    {
    case TurretRequestOutcome::Took:
        log.Info("TemplateGame: peer {} took the turret", message.From.Value);
        return true;
    case TurretRequestOutcome::Left:
        log.Info("TemplateGame: peer {} left the turret", message.From.Value);
        return true;
    case TurretRequestOutcome::Occupied:
    case TurretRequestOutcome::Invalid:
        break;
    }
    return false;
}

namespace
{
// How far from whoever asked a placed turret stands. Far enough not to be
// inside them, near enough to be the one they get when they ask again.
inline constexpr float kTurretPlacementReach = 3.0f;

// Where the player at this machine is standing, or none when nobody is. A
// dedicated host is the second case and is not an error there: it has no
// player of its own, and everything below still has to happen somewhere.
std::optional<Vec3d> LocalPlayerPosition(const World& world)
{
    const EntityId subject = LocalControlSubjectOf(world);
    if (!subject.IsValid())
        return std::nullopt;
    if (const LocalTransform* here = world.TryGet<LocalTransform>(subject))
        return here->Value.Position;
    return std::nullopt;
}

// Where a turret goes when somebody asks this machine for one: beside whoever
// is playing here, or beside where the level says players begin, so a host
// with nobody at it still puts the gun where people will arrive.
Vec3d TurretPlacementNear(const World& world)
{
    Vec3d at = LocalPlayerPosition(world).value_or(
        FindPlayerStart(world, std::nullopt).value_or(kDefaultPlayerStart));
    at.X += kTurretPlacementReach;
    return at;
}

// Putting one down, wherever this machine would put one. None when there is
// nowhere to put it yet, which is the only way it fails.
//
// Authority-only, structurally: both callers are the authority answering a
// request, and a client answers none.
// A placement either lands now (the procedural turret) or is in flight (the
// settings-named prefab, settling at a later frame's drain).
struct TurretPlacementOutcome
{
    EntityId Turret;
    bool Requested = false;
};

TurretPlacementOutcome PlaceTurretForRequest(
    Engine& engine, World& world, const CompiledGameSettings* settings,
    Logger& log)
{
    const PlayContentPartition* content =
        world.TryGetResource<PlayContentPartition>();
    if (content == nullptr)
        return {};

    if (settings != nullptr && !settings->TurretScenePath.empty())
    {
        Transform3f root = Transform3f::Identity();
        root.Position = TurretPlacementNear(world);
        const SceneSpawnId id = engine.Spawns().RequestSpawn(
            settings->TurretScenePath, root,
            content->Value.value_or(PersistentStoragePartition));
        PendingSpawnsOf(world).Turrets.push_back({ id, EntityId{} });
        log.Info("TemplateGame: placing a turret");
        return { EntityId{}, true };
    }

    const EntityId turret =
        PlaceTurret(world, TurretPlacementNear(world),
                    content->Value.value_or(PersistentStoragePartition));
    log.Info("TemplateGame: placed a turret");
    return { turret, false };
}

// What both console paths say when there is nowhere to put one yet.
constexpr std::string_view kNoContentForTurret =
    "no loaded content to put a turret in";

} // namespace

// The client half of the request: name the turret in terms the authority will
// recognise, and send. Nothing is decided here -- what comes back is a
// snapshot in which somebody drives something.
ConsoleResult AskAuthorityForTurret(Engine& engine, NetSession& session)
{
    ConsoleResult result;
    const World& world = engine.World().Entities();

    if (!session.IsConnected())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("not connected to an authority");
        return result;
    }

    // Already driving one: the same request means getting out, because the
    // authority holds the record of who drives what and can tell the
    // difference without being told.
    EntityId target = LocalControlSubjectOf(world);
    if (!target.IsValid() || !world.HasComponent<TurretMount>(target))
    {
        // Otherwise the nearest one to wherever this player is standing.
        target = NearestTurret(
            world, LocalPlayerPosition(world).value_or(Vec3d::Zero()));
    }

    if (!target.IsValid())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("no turret here");
        return result;
    }

    const NetEntityId named =
        engine.Replication().ClientEntities().TryFind(target);
    if (!named.IsValid())
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("that turret is not one replication gave this machine");
        return result;
    }

    // Reliable: a request that is dropped is a key press that did nothing, and
    // nothing later supersedes it. That is a fact about the message rather than
    // something the engine could decide.
    const std::size_t sent = NetSendToAuthority(
        session, NetChannelKind::ReliableOrdered, kTurretRequestKind,
        EncodeTurretRequest(named), &engine.NetTraffic());
    if (sent == 0)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("could not queue the request");
        return result;
    }

    result.Info("asked the authority about turret "
                + std::to_string(named.Value));
    return result;
}

// Putting one down without getting into it. Separate from taking one because
// they are separate operations with separate callers: a dedicated host places
// the gun its clients will ask for and has nobody to drive it, and conflating
// the two would mean a host could only provide a turret by occupying it.
//
// Authority-only, and that is structural rather than checked twice: this is
// the machine that decides what exists, and a client's turrets arrive
// replicated.
ConsoleResult PlaceTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log)
{
    ConsoleResult result;
    const TurretPlacementOutcome placed = PlaceTurretForRequest(
        engine, engine.World().Entities(), settings, log);
    if (!placed.Turret.IsValid() && !placed.Requested)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error(std::string(kNoContentForTurret));
        return result;
    }
    result.Info(placed.Requested ? "placing a turret" : "placed a turret");
    return result;
}

// The same request where this process is the authority the message would have
// gone to, so it answers directly. A standalone game has no session at all; a
// host has one and is still what its own player asks.
//
// Asking for a turret where there is none puts one down, so a player alone
// with a console reaches the possession path in one command. A host wanting a
// gun it does not climb into asks for that instead.
ConsoleResult TakeTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log)
{
    ConsoleResult result;
    World& world = engine.World().Entities();

    const EntityId participant = LocalParticipantOf(world);
    const ParticipantControl* control =
        participant.IsValid()
            ? world.TryGet<ParticipantControl>(participant)
            : nullptr;
    if (control == nullptr)
    {
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("nobody is playing at this machine");
        return result;
    }
    // Read out now: placing a turret below is structural, and anything after
    // that would be reading a row that has moved.
    EntityId target = control->ControlSubject;

    // Already driving one: the same request means getting out. Otherwise the
    // nearest one to wherever this player is standing, and one put down for
    // them when the level has none.
    if (!target.IsValid() || !world.HasComponent<TurretMount>(target))
    {
        const Vec3d from = LocalPlayerPosition(world).value_or(Vec3d::Zero());
        target = NearestTurret(world, from);
        if (!target.IsValid())
        {
            // A placement already in flight is claimed rather than doubled;
            // otherwise place one, and ride it if the prefab path made that
            // asynchronous.
            PendingSceneSpawns& pending = PendingSpawnsOf(world);
            const auto inFlight = std::find_if(
                pending.Turrets.begin(), pending.Turrets.end(),
                [&](const PendingSceneSpawns::TurretRequest& request)
                {
                    return !request.Possessor.IsValid()
                        || request.Possessor == participant;
                });
            if (inFlight != pending.Turrets.end())
            {
                inFlight->Possessor = participant;
                result.Info("a turret is being placed; taking it when it lands");
                return result;
            }
            const TurretPlacementOutcome placed = PlaceTurretForRequest(
                engine, world, settings, log);
            if (placed.Requested)
            {
                PendingSpawnsOf(world).Turrets.back().Possessor = participant;
                result.Info("a turret is being placed; taking it when it lands");
                return result;
            }
            target = placed.Turret;
        }
        if (!target.IsValid())
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error(std::string(kNoContentForTurret));
            return result;
        }
    }

    switch (ApplyTurretRequest(engine, world, participant, target, PeerId{}))
    {
    case TurretRequestOutcome::Took:
        result.Info("took the turret");
        break;
    case TurretRequestOutcome::Left:
        result.Info("left the turret");
        break;
    case TurretRequestOutcome::Occupied:
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("somebody else is at that turret");
        break;
    case TurretRequestOutcome::Invalid:
        result.Status = ConsoleStatus::InvalidArguments;
        result.Error("no turret here");
        break;
    }
    return result;
}

void TurretAimSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<TurretMount>() || !world.IsRegistered<LookOrientation>())
        return;

    Query<Write<TurretMount>, Read<LookOrientation>> query(world);
    query.ForEachChunk([](auto& view) {
        auto mounts = view.template Write<TurretMount>();
        auto looks = view.template Read<LookOrientation>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            // Look yaw is a running total and grows without bound; a
            // turret points somewhere, so it is wrapped here rather than
            // sent at a resolution that has to cover every turn a player
            // has ever made.
            constexpr float pi = std::numbers::pi_v<float>;
            float yaw = std::fmod(looks[index].Yaw + pi, 2.0f * pi);
            if (yaw < 0.0f)
                yaw += 2.0f * pi;
            mounts[index].Yaw = yaw - pi;
        }
    });
}
