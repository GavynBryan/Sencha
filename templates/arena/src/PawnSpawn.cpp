#include "PawnSpawn.h"

#include "LocalLookFollow.h"
#include "ArenaStart.h"
#include "ArenaSettingsData.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <net/NetParticipantIdentity.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <net/NetSpawnPrefab.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <vector>


// Where a level says players begin, or none when it does not say. The two
// answers are kept apart rather than folded into a default here, because a
// level with no start and a level whose start happens to be at the default are
// the same picture from the outside and want different things said about them.
std::optional<Vec3d> FindPlayerStart(
    const World& world,
    std::optional<StoragePartitionId> partition)
{
    if (!world.IsRegistered<ArenaStart>())
        return std::nullopt;

    for (EntityId entity : world.GetAliveEntities())
    {
        if (partition.has_value()
            && world.GetEntityPartition(entity) != *partition)
        {
            continue;
        }
        if (!world.HasComponent<ArenaStart>(entity))
            continue;

        if (const LocalTransform* transform =
                world.TryGet<LocalTransform>(entity))
        {
            return transform->Value.Position;
        }
    }
    return std::nullopt;
}

EntityId CreateTransformEntity(
    World& world,
    const Vec3d& position,
    StoragePartitionId partition,
    const Vec3d& scale)
{
    Transform3f transform;
    transform.Position = position;
    transform.Scale = scale;

    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    // WorldTransform is owed by the local one, not written by whoever happens
    // to place an entity; the engine states that obligation in one place.
    SeedDerivedWorldTransform(world, entity);
    return entity;
}

// The spawned group's root: the member without a parent. A prefab meant to be
// spawned as one thing has exactly one; content that ships more is taken by
// its first.
EntityId SpawnedGroupRoot(const World& world, std::span<const EntityId> members)
{
    for (EntityId member : members)
        if (world.TryGet<Parent>(member) == nullptr)
            return member;
    return {};
}

// Names the prefab a replicated body came from, so a peer instantiates the
// same one instead of being handed loose components to reassemble. Read off the
// group identity the spawn already stamped: the prefab's own asset id.
//
// A body built in code has none, and cannot get one -- there is no asset to
// name. It still replicates its state; it simply arrives on a peer with no
// body, which is the honest consequence of a game whose content did not load.
void StampNetPrefab(World& world, EntityId root, Logger& log)
{
    const SceneInstance* group = world.TryGet<SceneInstance>(root);
    if (group == nullptr || !group->Source.IsValid())
    {
        log.Warn("ArenaGame: a replicated body has no prefab identity; peers "
                 "will see its state and no body");
        return;
    }
    if (!world.HasComponent<NetSpawnPrefab>(root))
    {
        world.AddComponent<NetSpawnPrefab>(root,
                                           NetSpawnPrefab{ .Scene = group->Source });
    }
}

// Content has arrived, so anybody admitted before it can have a body now.
//
// The engine asks once, at admission, and never again on its own -- which is
// what keeps "waiting for a map to load" from being indistinguishable from
// "spectating for good". Asking again is the game's call, and this is the
// moment the answer changes.
void RequestBodiesForWaitingParticipants(Engine& engine)
{
    // This machine's own person, if it has one and is the authority for it.
    // The engine decides both; the game only knows when there is somewhere to
    // put a body, which is now.
    (void)engine.AdmitLocalParticipant();

    World& world = engine.World().Entities();
    if (!world.IsRegistered<ParticipantControl>())
        return;

    std::vector<EntityId> waiting;
    const World& reading = world;
    reading.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl&) {
            waiting.push_back(participant);
        });

    for (const EntityId participant : waiting)
        (void)engine.RequestParticipantBody(participant);
}

void PublishPlayContent(World& world, std::optional<StoragePartitionId> partition)
{
    if (PlayContentPartition* existing = world.TryGetResource<PlayContentPartition>())
        existing->Value = partition;
    else
        world.AddResource<PlayContentPartition>().Value = partition;
}

std::optional<BodySpawnRequest> ChoosePawnSpawn(
    const World& world, EntityId participant,
    const CompiledArenaSettings* settings, Logger& log)
{
    // Nowhere to put a body until content has loaded. Content that has been
    // and gone leaves the resource behind with no partition in it; what proves
    // there is somewhere for a body is the value, not the resource.
    const PlayContentPartition* content = world.TryGetResource<PlayContentPartition>();
    if (content == nullptr || !content->Value.has_value())
        return std::nullopt;

    // No prefab is no body. Said at Error rather than papered over with a
    // built-in one: a player driving a diagnostic capsule while the game
    // believes it is running is exactly what must not pass unremarked, and a
    // game with no pawn content is a game that is not set up yet.
    if (settings == nullptr || settings->PlayerPawnScenePath.empty())
    {
        log.Error("ArenaGame: no player pawn prefab configured "
                  "(game.settings player_pawn); nobody gets a body");
        return std::nullopt;
    }

    // Unfiltered: a map's content is imported into its own zone partition, so
    // a start looked for only in the persistent one is a start that is never
    // found and a peer that arrives at the origin.
    const std::optional<Vec3d> authored = FindPlayerStart(world, std::nullopt);
    // Said out loud once per spawn, because everything downstream of it looks
    // exactly like a level that authored a start at the origin -- including
    // anything else near where a player begins.
    if (!authored.has_value())
    {
        log.Warn("ArenaGame: no player_start in the loaded content; "
                 "spawning at the default position");
    }

    // Offset laterally from the start so two players do not arrive inside
    // each other, by peer id so somebody lands in the same place however many
    // others are present. A proper multi-start rotation is the level's
    // business, not this policy's.
    const NetParticipantIdentity* who = world.TryGet<NetParticipantIdentity>(participant);
    const std::uint32_t peer = who == nullptr ? 0u : who->Peer;
    Vec3d spawn = authored.value_or(kDefaultPlayerStart);
    spawn.X += 2.0f * static_cast<float>(peer);

    BodySpawnRequest request;
    request.ScenePath = settings->PlayerPawnScenePath;
    request.Root.Position = spawn;
    request.Partition = PersistentStoragePartition;
    return request;
}

// The prefab is the pawn: its mesh, controller, tuning, mode, aim, tags,
// attributes, and abilities are all authored, and the per-tick columns come
// with the movement component. What is left to do here is say so -- named
// rather than numbered for the one with no peer behind it, because peer zero
// is the authority, and "a pawn for peer 0" describes the person at this
// machine as a connection that does not exist.
void PreparePawn(World& world, EntityId participant, EntityId root, Logger& log)
{
    StampNetPrefab(world, root, log);
    const NetParticipantIdentity* who = world.TryGet<NetParticipantIdentity>(participant);
    const std::uint32_t peer = who == nullptr ? 0u : who->Peer;
    if (peer == kNetAuthorityPeer)
        log.Info("ArenaGame: spawned a pawn for the player at this machine (pawn prefab)");
    else
        log.Info("ArenaGame: spawned a pawn for peer {} (pawn prefab)", peer);
}

void SessionPlayerSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    // No role anywhere in here. Who provides participants and who receives
    // them replicated is the engine's decision, taken where the session
    // role is actually known; what is left is presenting whichever body
    // this machine ended up driving.
    FollowLocalControl(ctx.Entities);
}

// What this machine has to do about driving a pawn, once the engine has
// decided which one that is.
//
// This used to be a scan of every entity replication had created, looking
// for one whose NetOwner named this peer -- which is the question the
// engine now answers before the frame's first tick, and answers without an
// unordered_map walk whose winner changed with hash order.
void SessionPlayerSystem::FollowLocalControl(World& world)
{
    // Whose look input the body takes is this game's rule, applied here where
    // the game already watches the subject change.
    (void)FollowLocalLookControl(world, LookTagged);

    const EntityId subject = LocalControlSubjectOf(world);
    if (subject == Followed)
        return;
    Followed = subject;

    if (!subject.IsValid())
        return;

    // Nothing to build. A pawn that arrived replicated was instantiated
    // from the prefab the authority named, so it is already the same
    // archetype the authority is simulating -- which is what makes
    // predicting it from the same input produce the same pawn. The camera
    // that looks through it is PawnCameraSystem's.
    if (Owner->Prediction().Predicts(subject))
        Log->Info("ArenaGame: predicting this player's own pawn");
}

void SpawnSettlementSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    if (Owner == nullptr)
        return;

    const ZoneId play = Owner->Level().PlayZone();
    if (!play.IsValid())
        return;

    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        if (change.Zone != play)
            continue;

        if (change.Kind == ZoneResidencyChangeKind::Detaching)
        {
            PublishPlayContent(ctx.Entities, std::nullopt);
            if (Bodies != nullptr)
                Bodies->CancelAllPending();
            continue;
        }
        if (change.Kind != ZoneResidencyChangeKind::Attached)
            continue;

        // Where a pawn belongs, not a pawn. Who provides one is the session's
        // decision, taken every frame once this exists.
        PublishPlayContent(ctx.Entities, change.Partition);
        RequestBodiesForWaitingParticipants(*Owner);
    }
}

// After the drain where the spawn service publishes, before the session
// presents bodies: a pawn that lands this frame is followed this frame.
void SpawnSettlementSystem::FrameUpdate(FrameUpdateContext&)
{
    if (Bodies != nullptr)
        Bodies->Update();
}
