#include <gtest/gtest.h>

#include <assets/data/DataAssetHandle.h>
#include <controller/LookOrientation.h>
#include <movement/JumpState.h>
#include <movement/MovementComponents.h>
#include <movement/MovementProfileData.h>
#include <net/ClientPrediction.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <ecs/Query.h>
#include <net/NetSpawnPrefab.h>
#include "StubPrefabSpawner.h"
#include <net/ReplicationInterpolation.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationSchemas.h>
#include <net/ReplicationSnapshot.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformHistory.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    constexpr std::size_t kSnapshotBytes = 64 * 1024;

    // What the wire can express. A replicated position is not the authority's
    // float any more: it is the nearest value the declared precision can name,
    // so what these tests hold the wire to is that distance rather than
    // equality. Derived from the schema rather than written down, so widening
    // the field widens this with it and narrowing it cannot pass unnoticed.
    constexpr float WireStep(float range, std::uint8_t bits)
    {
        return 2.0f * range / static_cast<float>((std::uint32_t{ 1 } << bits) - 1u);
    }
    constexpr float kWirePosition = WireStep(kNetPositionRange, kNetPositionBits);

    // Two worlds built from the same sealed schema, wired together by hand so
    // the whole replication path runs with no sockets, no frame, and no clock.
    // The authority half and the client half never share a pointer into each
    // other: everything crosses as bytes.
    struct Pair
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Authority;
        World Client;

        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        std::uint64_t Generation = 0;
        ReplicationPeerState Peer;
        ReplicationClientIdentity ClientIdentity;

        std::vector<std::byte> Scratch;
        // How much of that scratch a snapshot may use. The default is far more
        // than any test world needs, so the budget is out of the way unless a
        // test is about it.
        std::size_t Budget = kSnapshotBytes;
        std::uint64_t Tick = 0;
        // Whether the client confirms what it applied. Off models a peer whose
        // acknowledgements are not arriving.
        bool Acknowledge = true;
        // Whether the client receives the snapshot at all. False is a datagram
        // lost on the wire: written, never applied, never confirmed.
        bool Deliver = true;
        // What the client would report: the snapshots it actually applied.
        NetSnapshotAck ClientAck;
        SnapshotWriteResult LastWrite;
        SnapshotApplyResult LastApply;

        Pair() : Scratch(kSnapshotBytes)
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Authority);
            Schema.Apply(Client);
            Layout.Seal();
        }

        // One snapshot: written on the authority, carried as bytes, applied on
        // the client. Returns the bytes it took.
        std::size_t Replicate(std::uint32_t ownerPeer = 0,
                              INetPrefabSpawner* prefabs = nullptr,
                              ClientPrediction* prediction = nullptr,
                              ReplicationInterpolation* interpolation = nullptr,
                              std::uint64_t commandAck = 0)
        {
            ++Tick;
            Changes.Update(Authority, Layout, Identity, ++Generation);

            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.OwnerPeer = ownerPeer;
            write.Tick = Tick;
            write.Sequence = Peer.NextSnapshotSequence();
            write.CommandAck = commandAck;

            LastWrite = ReplicationWriteSnapshot(
                write, std::span(Scratch).subspan(0, Budget));
            EXPECT_TRUE(LastWrite.Ok);

            if (!Deliver)
                return LastWrite.BytesWritten;

            SnapshotApplyRequest apply;
            apply.Target = &Client;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIdentity;
            apply.Prefabs = prefabs;
            apply.Prediction = prediction;
            apply.Interpolation = interpolation;

            LastApply = ReplicationApplySnapshot(
                apply, std::span(Scratch).subspan(0, LastWrite.BytesWritten));
            EXPECT_TRUE(LastApply.Ok())
                << SnapshotApplyErrorToString(LastApply.Error);

            // The client applied it, so it says so. Deltas are measured from
            // what a peer has confirmed, and a harness that never confirmed
            // would be testing a peer that never answers -- which has its own
            // test below.
            // Complete rather than Ok, for the reason ReplicationRuntime uses
            // the same predicate: a snapshot that deferred an authored entity
            // left a consistent world and still did not land everything it
            // described, and confirming it would raise a floor past state the
            // client does not hold.
            if (Acknowledge && LastApply.Complete())
            {
                ClientAck.Observe(LastApply.Sequence);
                Peer.Acknowledge(ClientAck);
            }
            return LastWrite.BytesWritten;
        }

        EntityId SpawnReplicated(const Transform3f& pose)
        {
            const EntityId entity = Authority.CreateEntity();
            Authority.AddComponent<NetReplicated>(entity);
            Authority.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
            return entity;
        }

        // The client entity standing in for an authority one, or invalid.
        [[nodiscard]] EntityId Mirror(EntityId authorityEntity) const
        {
            const NetEntityId id = Identity.TryFind(authorityEntity);
            return id.IsValid() ? ClientIdentity.TryResolve(id) : EntityId{};
        }
    };

    Transform3f PoseAt(float x, float y, float z)
    {
        Transform3f pose;
        pose.Position = Vec3d{ x, y, z };
        return pose;
    }
}

TEST(ReplicationSnapshot, AnEntityMarkedForReplicationAppearsOnTheClient)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 2.0f, 3.0f));

    pair.Replicate();

    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 1u);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    const LocalTransform* transform = pair.Client.TryGet<LocalTransform>(mirror);
    ASSERT_NE(transform, nullptr);
    EXPECT_NEAR(transform->Value.Position.X, 1.0f, kWirePosition);
    EXPECT_NEAR(transform->Value.Position.Y, 2.0f, kWirePosition);
    EXPECT_NEAR(transform->Value.Position.Z, 3.0f, kWirePosition);
}

// The whole point of the marker: a level's worth of authored geometry has
// transforms and must not cost a byte.
TEST(ReplicationSnapshot, UnmarkedEntitiesAreNotReplicated)
{
    Pair pair;
    for (int i = 0; i < 50; ++i)
    {
        const EntityId scenery = pair.Authority.CreateEntity();
        pair.Authority.AddComponent<LocalTransform>(
            scenery, LocalTransform{ PoseAt(static_cast<float>(i), 0.0f, 0.0f) });
    }

    pair.Replicate();

    EXPECT_EQ(pair.LastWrite.EntitiesWritten, 0u);
    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 0u);
    EXPECT_EQ(pair.ClientIdentity.Size(), 0u);
}

TEST(ReplicationSnapshot, MovementOnTheAuthorityFollowsToTheClient)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    for (int step = 1; step <= 30; ++step)
    {
        pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
            Vec3d{ static_cast<float>(step), 0.0f, static_cast<float>(step) * 0.5f };
        pair.Replicate();

        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr);
        ASSERT_NEAR(seen->Value.Position.X, static_cast<float>(step), kWirePosition)
            << "step " << step;
    }

    // The mirror is the same entity throughout: identity is stable, so nothing
    // was respawned behind our backs.
    EXPECT_EQ(pair.Mirror(authority), mirror);
    EXPECT_EQ(pair.ClientIdentity.Size(), 1u);
}

TEST(ReplicationSnapshot, DestroyingOnTheAuthorityDestroysOnTheClient)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(5.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_TRUE(pair.Client.IsAlive(mirror));

    pair.Authority.DestroyEntity(authority);
    pair.Replicate();

    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 1u);
    EXPECT_EQ(pair.LastApply.EntitiesDestroyed, 1u);
    EXPECT_FALSE(pair.Client.IsAlive(mirror));
    EXPECT_EQ(pair.ClientIdentity.Size(), 0u);
    EXPECT_EQ(pair.Peer.Size(), 0u) << "a destroyed entity must leave no baseline";
}

// Identity is per session and nothing else ever names a destroyed entity, so
// the map has to shed them itself or it grows until the session ends.
TEST(ReplicationSnapshot, IdentityDoesNotAccumulateDestroyedEntities)
{
    Pair pair;
    for (int round = 0; round < 25; ++round)
    {
        const EntityId entity =
            pair.SpawnReplicated(PoseAt(static_cast<float>(round), 0.0f, 0.0f));
        pair.Replicate();
        pair.Authority.DestroyEntity(entity);
        pair.Replicate();
    }

    EXPECT_EQ(pair.Identity.Size(), 0u)
        << "the authority's identity map kept entities that no longer exist";
    EXPECT_EQ(pair.Peer.Size(), 0u);
    EXPECT_EQ(pair.ClientIdentity.Size(), 0u);
}

// Several entities, so the writer is exercised past the one-entity case and
// identity is shown not to cross over between them.
TEST(ReplicationSnapshot, ManyEntitiesKeepTheirOwnIdentities)
{
    Pair pair;
    std::vector<EntityId> authority;
    for (int i = 0; i < 8; ++i)
        authority.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0, 0)));

    pair.Replicate();
    ASSERT_EQ(pair.LastApply.EntitiesSpawned, 8u);

    for (int i = 0; i < 8; ++i)
    {
        const EntityId mirror = pair.Mirror(authority[static_cast<std::size_t>(i)]);
        ASSERT_TRUE(mirror.IsValid()) << i;
        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr) << i;
        EXPECT_NEAR(seen->Value.Position.X, static_cast<float>(i), kWirePosition);
    }

    // Destroy every other one and confirm the survivors are untouched.
    for (std::size_t i = 0; i < authority.size(); i += 2)
        pair.Authority.DestroyEntity(authority[i]);
    pair.Replicate();

    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 4u);
    for (std::size_t i = 1; i < authority.size(); i += 2)
    {
        const EntityId mirror = pair.Mirror(authority[i]);
        ASSERT_TRUE(mirror.IsValid()) << i;
        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr) << i;
        EXPECT_NEAR(seen->Value.Position.X, static_cast<float>(i), kWirePosition);
    }
}

// A component added after the entity is already known must reach the client
// too: an entity's shape is not fixed at spawn.
TEST(ReplicationSnapshot, AComponentAddedLaterStillArrives)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_FALSE(pair.Client.HasComponent<LookOrientation>(mirror));

    pair.Authority.AddComponent<LookOrientation>(
        authority, LookOrientation{ .Yaw = 1.5f, .Pitch = 0.25f });
    pair.Replicate();

    const LookOrientation* look = pair.Client.TryGet<LookOrientation>(mirror);
    ASSERT_NE(look, nullptr);
    EXPECT_FLOAT_EQ(look->Yaw, 1.5f);
    EXPECT_NEAR(look->Pitch, 0.25f, 0.001f);
}

// The delta property end to end: a still world costs the frame's bookkeeping
// and no field bits at all.
TEST(ReplicationSnapshot, AStillWorldCostsNothingAfterTheFirstSnapshot)
{
    Pair pair;
    for (int i = 0; i < 8; ++i)
        pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f));

    const std::size_t first = pair.Replicate();
    const std::size_t second = pair.Replicate();
    const std::size_t third = pair.Replicate();

    // Not "almost nothing" -- nothing. An entity with nothing to say is absent
    // rather than present with every mask clear, so a still world costs the
    // header and no more however many entities are standing in it.
    EXPECT_EQ(pair.LastWrite.EntitiesWritten, 0u);

    EXPECT_LT(second, first)
        << "the second snapshot of an unchanged world must be smaller than the first";
    EXPECT_EQ(second, third) << "and steady from then on";
}

// Absence has to mean "unchanged", not "gone". An entity the snapshot has
// nothing to say about must be left exactly as it was -- the client's only
// other reading of silence would be to destroy it, and the two are told apart
// by the destroy list carrying it or not.
TEST(ReplicationSnapshot, AnEntityLeftOutOfASnapshotIsLeftAloneNotDestroyed)
{
    Pair pair;
    const EntityId still = pair.SpawnReplicated(PoseAt(1.0f, 2.0f, 3.0f));
    const EntityId moving = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId stillMirror = pair.Mirror(still);
    const EntityId movingMirror = pair.Mirror(moving);
    ASSERT_TRUE(stillMirror.IsValid());
    ASSERT_TRUE(movingMirror.IsValid());

    for (int step = 1; step <= 4; ++step)
    {
        pair.Authority.TryGet<LocalTransform>(moving)->Value.Position =
            Vec3d{ static_cast<float>(step), 0.0f, 0.0f };
        pair.Replicate();
        EXPECT_EQ(pair.LastWrite.EntitiesWritten, 1u)
            << "the entity that did not move was still described";
        EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 0u);
    }

    EXPECT_TRUE(pair.Client.IsAlive(stillMirror))
        << "an entity was destroyed for having nothing to report";
    const Vec3d& held = pair.Client.TryGet<LocalTransform>(stillMirror)->Value.Position;
    EXPECT_NEAR(held.X, 1.0f, kWirePosition);
    EXPECT_NEAR(held.Y, 2.0f, kWirePosition);
    EXPECT_NEAR(held.Z, 3.0f, kWirePosition);
    const Vec3d& moved = pair.Client.TryGet<LocalTransform>(movingMirror)->Value.Position;
    EXPECT_NEAR(moved.X, 4.0f, kWirePosition);
    EXPECT_NEAR(moved.Y, 0.0f, kWirePosition);
    EXPECT_NEAR(moved.Z, 0.0f, kWirePosition);
}

// A peer that was never told about an entity has nothing to forget, and one
// whose destroy was lost still needs telling -- so which destroys a snapshot
// carries is a fact about the peer, not about the world.
TEST(ReplicationSnapshot, ADestroyIsResentUntilThePeerConfirmsIt)
{
    Pair pair;
    const EntityId doomed = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(doomed);
    ASSERT_TRUE(mirror.IsValid());

    // The destroy goes out and is lost on the wire.
    pair.Authority.DestroyEntity(doomed);
    pair.Deliver = false;
    pair.Replicate();
    pair.Deliver = true;
    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 1u);
    EXPECT_TRUE(pair.Client.IsAlive(mirror)) << "the harness delivered a dropped snapshot";

    // The next one says it again, because nothing confirmed the first.
    pair.Replicate();
    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 1u)
        << "a destroy the peer never confirmed was said once and forgotten";
    EXPECT_FALSE(pair.Client.IsAlive(mirror));

    // And once it has landed, it stops being said.
    pair.Replicate();
    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 0u);
}

// Only the entity that moved should cost field bits.
TEST(ReplicationSnapshot, OnlyWhatMovedCostsAnything)
{
    Pair pair;
    std::vector<EntityId> entities;
    for (int i = 0; i < 8; ++i)
        entities.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0, 0)));

    pair.Replicate();
    const std::size_t idle = pair.Replicate();

    pair.Authority.TryGet<LocalTransform>(entities[3])->Value.Position =
        Vec3d{ 99.0f, 0.0f, 0.0f };
    const std::size_t moved = pair.Replicate();

    EXPECT_GT(moved, idle);
    const EntityId mirror = pair.Mirror(entities[3]);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 99.0f, kWirePosition);

    // And the world settles back down once it stops moving.
    EXPECT_EQ(pair.Replicate(), idle);
}

TEST(ReplicationSnapshot, OwnerOnlyStateReachesOnlyItsOwner)
{
    // No engine component has an owner-only field yet, so this drives the
    // mechanism through the writer with a layout built for the test.
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 7 });

    pair.Replicate(7);

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    const NetOwner* owner = pair.Client.TryGet<NetOwner>(mirror);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->Peer, 7u)
        << "a client has to learn which entity is its own";
}

//=============================================================================
// Prediction
//
// A client's own pawn is the one entity it simulates rather than mirrors, so
// the authority's position for it is an argument to settle rather than state to
// write. These assert that through the applier, which is where the subtle part
// lives: a delta carries only what changed, so it has to be applied to what the
// authority believes it sent.
//=============================================================================

TEST(ReplicationPrediction, ThePredictedEntityKeepsWhatThisMachineSimulated)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    prediction.SetPredicted(mirror);

    // This machine has simulated the pawn forward; the authority has not caught
    // up yet. Its word must not overwrite what was simulated.
    pair.Client.TryGet<LocalTransform>(mirror)->Value.Position =
        Vec3d{ 9.0f, 0.0f, 0.0f };
    pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
        Vec3d{ 1.0f, 0.0f, 0.0f };
    pair.Replicate(0, nullptr, &prediction);

    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    9.0f, kWirePosition)
        << "the authority's position was written over what this machine "
           "predicted, which is the round trip the player would feel";
}

// The trap the authoritative shadow exists for: the world's copy of a predicted
// pawn is this machine's guess, and staging an arriving delta against a guess
// makes a divergence read as agreement. What the shadow holds has to be what
// the authority said, however far the guess has wandered.
TEST(ReplicationPrediction, TheShadowHoldsTheAuthoritysWordNotThisMachinesGuess)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    prediction.SetPredicted(mirror);

    // The authority is at one metre along X. This machine has guessed its way
    // somewhere else entirely, which is what predicting ahead looks like.
    pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
        Vec3d{ 1.0f, 0.0f, 0.0f };
    pair.Client.TryGet<LocalTransform>(mirror)->Value.Position =
        Vec3d{ 40.0f, 0.0f, -12.0f };
    pair.Replicate(0, nullptr, &prediction);

    // The guess is untouched -- the whole point of intercepting.
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 40.0f, kWirePosition);

    // And the authority's word is what was kept, so a replay resumes from a
    // real position rather than from wherever this machine had drifted.
    const EntityId probe = pair.Client.CreateEntity();
    pair.Client.AddComponent<LocalTransform>(probe, LocalTransform{});
    ASSERT_TRUE(prediction.RestoreTo(pair.Client, pair.Schema, probe));
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(probe)->Value.Position.X,
                    1.0f, kWirePosition)
        << "the shadow was contaminated by the world's copy, so every delta "
           "after this one is staged against a value the authority never sent";
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(probe)->Value.Position.Z, 0.0f, kWirePosition);
}

// A client cannot replay what it cannot separate from what has been answered,
// so the acknowledgement travels with the state it accounts for -- in the same
// message, describing the same moment.
TEST(ReplicationSnapshot, CarriesHowFarTheAuthorityGotThroughThisClientsInput)
{
    Pair pair;
    (void)pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));

    pair.Replicate(0, nullptr, nullptr, nullptr, 4321);
    EXPECT_EQ(pair.LastApply.CommandAck, 4321u);

    // And it moves with the authority rather than sticking at the first value.
    pair.Replicate(0, nullptr, nullptr, nullptr, 4400);
    EXPECT_EQ(pair.LastApply.CommandAck, 4400u);
}

//=============================================================================
// Deltas are measured from what the peer confirmed, not from what was sent
//
// The defect this closes cost a whole session's worth of state for one dropped
// datagram. A snapshot rides an unreliable channel, so writing one is not
// delivering it; a baseline advanced on writing then describes a state the peer
// never reached, and every later difference is measured from that fiction. A
// position hides it -- it changes every tick, so every snapshot re-carries it.
// Anything that settles does not.
//=============================================================================

TEST(ReplicationAcknowledgement, StateSurvivesASnapshotThatNeverArrives)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 7 });
    pair.Authority.AddComponent<CharacterMovement>(
        authority, CharacterMovement{ .Mode = LocomotionModeId{ 3 } });

    // Settled and confirmed: from here the authority is entitled to stop
    // mentioning the mode, which is what makes the rest of this a test.
    pair.Replicate(7);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 3u);

    // The mode changes, and the snapshot carrying it never arrives -- written,
    // dropped on the wire, never applied and never confirmed.
    pair.Authority.TryGet<CharacterMovement>(authority)->Mode = LocomotionModeId{ 5 };
    pair.Deliver = false;
    pair.Replicate(7);
    pair.Deliver = true;
    ASSERT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 3u)
        << "the harness delivered a snapshot it was told to drop";

    // Everything after it arrives normally. The mode never changes again, so
    // only measuring differences from what this client confirmed can bring it.
    for (int step = 0; step < 3; ++step)
    {
        pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
            Vec3d{ static_cast<float>(step), 0.0f, 0.0f };
        pair.Replicate(7);
    }

    EXPECT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 5u)
        << "a lost snapshot took a settled value with it for the rest of the "
           "session, because the authority went on describing differences from "
           "a state this client was never in";
}

// The other half of the same rule: a peer that keeps confirming pays for what
// it already has exactly once.
// Promotion needs the acknowledgement to name the snapshot, not merely to be
// newer than it. A cumulative mark vouches for everything behind it, including
// the datagrams that never landed -- and the peer is then recorded as holding
// bytes it was never sent.
//
// Under a scheme where every snapshot carries every entity this is harmless,
// because the next one restates the mistake away. It stops being harmless the
// moment a snapshot may leave something out, which is what makes this the
// foundation the omit-unchanged work is built on rather than a fix for a
// visible bug today.
TEST(ReplicationAcknowledgement, ALostSnapshotIsNotPromotedByALaterOne)
{
    ReplicationPeerState peer;
    const NetEntityId lostEntity{ 1 };
    const NetEntityId arrivedEntity{ 2 };

    // Two snapshots carrying different entities, so what each one carried is
    // distinguishable in the floors afterwards.
    const std::uint32_t lost = peer.NextSnapshotSequence();
    peer.BeginSnapshot(lost);
    peer.RecordSent(lost, lostEntity, 10);

    const std::uint32_t arrived = peer.NextSnapshotSequence();
    peer.BeginSnapshot(arrived);
    peer.RecordSent(arrived, arrivedEntity, 11);

    // The client applied only the second. It says so, and says nothing about
    // the first, because it never saw it.
    NetSnapshotAck ack;
    ack.Observe(arrived);
    peer.Acknowledge(ack);

    EXPECT_EQ(peer.Floor(arrivedEntity), 11u)
        << "the snapshot the client confirmed did not raise its floor";
    EXPECT_EQ(peer.Floor(lostEntity), 0u)
        << "a snapshot the client never received was counted as history it holds";

    // And nothing is left waiting on proof that can no longer arrive.
    EXPECT_EQ(peer.Unacknowledged(), 0u);
}

// A value that changes and changes back inside one round trip.
//
// Asking "does this differ from what the peer holds" answers no -- the
// authority is back where it started, so a comparison against the peer's bytes
// sees nothing to send. But the peer applied the middle of the excursion and is
// sitting on it, and nothing will ever move that value again. Asking "has this
// moved since they last proved they were up to date" is the question with no
// such hole, which is why the store records when a run last moved rather than
// what each peer was last told.
TEST(ReplicationAcknowledgement, AValueThatChangesAndChangesBackStillArrives)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 7 });
    pair.Authority.AddComponent<CharacterMovement>(
        authority, CharacterMovement{ .Mode = LocomotionModeId{ 3 } });

    // Settled and confirmed at three.
    pair.Replicate(7);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 3u);

    // It changes to five. That snapshot arrives -- the client now holds five --
    // but its acknowledgement is lost, so the authority has no proof.
    pair.Authority.TryGet<CharacterMovement>(authority)->Mode = LocomotionModeId{ 5 };
    pair.Acknowledge = false;
    pair.Replicate(7);
    pair.Acknowledge = true;
    ASSERT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 5u);

    // And then it changes straight back to three, which is where the authority
    // believed the client already was.
    pair.Authority.TryGet<CharacterMovement>(authority)->Mode = LocomotionModeId{ 3 };
    for (int step = 0; step < 3; ++step)
    {
        pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
            Vec3d{ static_cast<float>(step), 0.0f, 0.0f };
        pair.Replicate(7);
    }

    EXPECT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 3u)
        << "the client was left holding the middle of an excursion the "
           "authority had already come back from, because nothing differed "
           "from what it was believed to hold";
}

// Owner-gated fields are the one thing a run's own history cannot account for.
// A transfer makes them newly visible to one peer and newly hidden from
// another without any of them having changed value, so an entity whose
// ownership moves must re-state them to whoever can now see them.
TEST(ReplicationAcknowledgement, OwnershipMovingResendsWhatTheNewOwnerMaySee)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 1 });
    pair.Authority.AddComponent<CharacterMovement>(
        authority, CharacterMovement{ .Mode = LocomotionModeId{ 4 } });

    // Peer 2 is a spectator here: the mode is owner-only, so it is gated out
    // and the mode never reaches this client while peer 1 owns the entity.
    pair.Replicate(2);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 0u)
        << "an owner-only field reached a peer that does not own the entity";

    // Ownership transfers to peer 2. The mode has not changed value since the
    // last snapshot, so only ownership having moved can bring it.
    pair.Authority.TryGet<NetOwner>(authority)->Peer = 2;
    pair.Replicate(2);

    EXPECT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 4u)
        << "a new owner was never told the owner-only state it had just become "
           "entitled to, because the field itself had not moved";
}

TEST(ReplicationAcknowledgement, ConfirmedStateIsNotSentAgain)
{
    Pair pair;
    (void)pair.SpawnReplicated(PoseAt(1.0f, 2.0f, 3.0f));

    const std::size_t first = pair.Replicate();
    const std::size_t second = pair.Replicate();
    EXPECT_LT(second, first)
        << "a world that did not move cost as much to describe the second time";
}

// A peer that stops answering cannot be tracked forever. Rather than grow, the
// authority forgets what it believed and tells that peer everything again.
TEST(ReplicationAcknowledgement, APeerThatStopsConfirmingIsToldEverythingAgain)
{
    Pair pair;
    (void)pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();

    pair.Acknowledge = false;
    for (std::size_t step = 0; step < ReplicationPeerState::kMaxUnacknowledged + 4;
         ++step)
    {
        pair.Replicate();
    }

    EXPECT_LE(pair.Peer.Unacknowledged(),
              ReplicationPeerState::kMaxUnacknowledged + 1)
        << "the authority kept every unconfirmed snapshot, so a silent peer "
           "buys memory on the machine hosting it";
}

// What running the ring out costs is the oldest unconfirmed snapshot, and only
// that. Proof a peer has already given does not decay: throwing its floors away
// means describing the entire world to it again at the exact moment its link is
// least able to carry that, and under a byte budget the seeding competes for
// room with the state that has genuinely moved.
TEST(ReplicationAcknowledgement, GoingQuietDoesNotUndoWhatAPeerAlreadyProved)
{
    Pair pair;
    const EntityId entity = pair.SpawnReplicated(PoseAt(1.0f, 2.0f, 3.0f));
    pair.Replicate();

    const NetEntityId id = pair.Identity.TryFind(entity);
    ASSERT_TRUE(id.IsValid());
    const std::uint64_t proved = pair.Peer.Floor(id);
    ASSERT_GT(proved, 0u);

    // Silence, for longer than the ring is deep. The world does not move, so
    // there is nothing new to say about it and every snapshot should be empty.
    pair.Acknowledge = false;
    for (std::size_t step = 0; step < ReplicationPeerState::kMaxUnacknowledged + 4;
         ++step)
    {
        pair.Replicate();
        ASSERT_EQ(pair.LastWrite.EntitiesWritten, 0u)
            << "step " << step << ": a still world was described again to a peer "
               "that had already confirmed holding it";
    }

    EXPECT_EQ(pair.Peer.Floor(id), proved);
}

//=============================================================================
// The pawn state that rides home to its owner
//
// A client resuming its own simulation needs everything the movement step
// reads: velocity, support, mode, jump cooldown. Nobody else does, and what one
// player's machine is owed must not be what every machine receives.
//=============================================================================

namespace
{
    // A pawn with the full movement-state set, owned by `peer`.
    EntityId SpawnOwnedPawn(Pair& pair, std::uint32_t peer)
    {
        const EntityId pawn = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
        pair.Authority.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = peer });
        // Velocity, support, and jump cooldown come with the movement
        // component; the values a snapshot has to carry are written onto them.
        pair.Authority.AddComponent<CharacterMovement>(
            pawn, CharacterMovement{ .Mode = LocomotionModeId{ 2 } });
        pair.Authority.TryGet<KinematicState>(pawn)->Velocity =
            Vec3d{ 3.0f, -1.0f, 0.5f };
        SupportState& support = *pair.Authority.TryGet<SupportState>(pawn);
        support.Kind = SupportKind::Stable;
        support.SurfaceVelocity = Vec3d{ 0.25f, 0.0f, 0.0f };
        pair.Authority.TryGet<JumpState>(pawn)->CooldownRemaining = 0.12f;
        return pawn;
    }
}

// A client's copy of a pawn is assembled from a snapshot, and a snapshot
// carries only what travels. Everything else the movement step reads -- this
// tick's request, the resolved coefficients, the contribution channels, the
// composed output -- is per-tick scratch that never goes on the wire, and a
// pawn missing any one of them stops matching the query that would have moved
// it. No error, no frame it happens on: the pawn simply stands still while its
// owner presses forward.
//
// It arrives because the movement component owes it, and replication creates
// components through the same typed add everything else does.
TEST(ReplicationPawnState, AReplicatedPawnArrivesAbleToMove)
{
    Pair pair;
    const EntityId pawn = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = 7 });
    pair.Authority.AddComponent<CharacterMovement>(
        pawn, CharacterMovement{ .Mode = LocomotionModeId{ 1 } });

    pair.Replicate(7);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());

    // None of these is replicated; all of them are owed.
    EXPECT_TRUE(pair.Client.HasComponent<MovementIntent>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<ResolvedMovementTuning>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<LocomotionOutput>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<MotionAxisOverride>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<MotionImpulse>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<MotionRequest>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<ModeTransitionRequest>(mirror));
    EXPECT_TRUE(pair.Client.HasComponent<WorldTransformHistory>(mirror));

    // At their initializers, which for the composed request is the difference
    // between standing still and being shoved toward the origin.
    EXPECT_EQ(pair.Client.TryGet<MotionRequest>(mirror)->UpAxis,
              MotionRequest{}.UpAxis);
    EXPECT_FLOAT_EQ(pair.Client.TryGet<ResolvedMovementTuning>(mirror)->MaxSpeed,
                    ResolvedMovementTuning{}.MaxSpeed);
}

TEST(ReplicationPawnState, TheOwnerGetsWhatItNeedsToResumeSimulating)
{
    Pair pair;
    const EntityId pawn = SpawnOwnedPawn(pair, 7);

    // This stream belongs to peer 7, the owner.
    pair.Replicate(7);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());

    const KinematicState* motion = pair.Client.TryGet<KinematicState>(mirror);
    ASSERT_NE(motion, nullptr);
    EXPECT_FLOAT_EQ(motion->Velocity.X, 3.0f);
    EXPECT_FLOAT_EQ(motion->Velocity.Y, -1.0f);

    const SupportState* support = pair.Client.TryGet<SupportState>(mirror);
    ASSERT_NE(support, nullptr);
    EXPECT_EQ(support->Kind, SupportKind::Stable);
    EXPECT_FLOAT_EQ(support->SurfaceVelocity.X, 0.25f);

    EXPECT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 2u);
    EXPECT_FLOAT_EQ(pair.Client.TryGet<JumpState>(mirror)->CooldownRemaining,
                    0.12f);
}

TEST(ReplicationPawnState, EveryoneElseGetsThePoseAndNoneOfTheRest)
{
    Pair pair;
    const EntityId pawn = SpawnOwnedPawn(pair, 7);
    pair.Authority.TryGet<LocalTransform>(pawn)->Value.Position =
        Vec3d{ 4.0f, 0.0f, 0.0f };

    // This stream belongs to peer 9, a spectator of pawn 7.
    pair.Replicate(9);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());

    // The pose travels to everyone: it is what the pawn looks like.
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 4.0f, kWirePosition);

    // The simulation state does not. The components exist -- the wire named
    // them -- but every owner-only field decoded to its default, which is what
    // an all-zero mask means.
    EXPECT_FLOAT_EQ(pair.Client.TryGet<KinematicState>(mirror)->Velocity.X, 0.0f)
        << "another player's machine was handed simulation state it has no "
           "business holding";
    EXPECT_EQ(pair.Client.TryGet<SupportState>(mirror)->Kind, SupportKind::None);
    EXPECT_EQ(pair.Client.TryGet<CharacterMovement>(mirror)->Mode.Value, 0u)
        << "a spectator's mirror claims a locomotion mode, so movement systems "
           "on that machine would start driving a puppet";
    EXPECT_FLOAT_EQ(pair.Client.TryGet<JumpState>(mirror)->CooldownRemaining, 0.0f);
}

// The shadow's whole reason applied to the new state: a delta that has no
// reason to resend velocity must not cost the predictor the velocity it was
// last told.
TEST(ReplicationPawnState, TheShadowKeepsStateAnEmptyDeltaDidNotResend)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);

    const EntityId pawn = SpawnOwnedPawn(pair, 7);
    pair.Replicate(7, nullptr, &prediction);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());
    prediction.SetPredicted(mirror);

    // The pawn moves, so the snapshot carries its state and the shadows fill.
    pair.Authority.TryGet<KinematicState>(pawn)->Velocity = Vec3d{ 3.0f, 0.0f, 0.0f };
    pair.Replicate(7, nullptr, &prediction);
    ASSERT_TRUE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<KinematicState>()));

    // The authority changes nothing, so the pawn is not mentioned at all.
    pair.Replicate(7, nullptr, &prediction);

    World scratch;
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    schema.Apply(scratch);
    const EntityId probe = scratch.CreateEntity();
    scratch.AddComponent<CharacterMovement>(probe, CharacterMovement{});
    scratch.AddComponent<LocalTransform>(probe, LocalTransform{});
    ASSERT_TRUE(prediction.RestoreTo(scratch, schema, probe));

    EXPECT_FLOAT_EQ(scratch.TryGet<KinematicState>(probe)->Velocity.X, 3.0f)
        << "an empty delta cost the shadow the velocity the authority last "
           "sent, so a replay would resume from a standstill";
    EXPECT_EQ(scratch.TryGet<SupportState>(probe)->Kind, SupportKind::Stable);
    EXPECT_FLOAT_EQ(scratch.TryGet<JumpState>(probe)->CooldownRemaining, 0.12f);
}

TEST(ReplicationPrediction, OtherPlayersPawnsStillArriveAsState)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);

    const EntityId mine = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    const EntityId theirs = pair.SpawnReplicated(PoseAt(5.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId myMirror = pair.Mirror(mine);
    const EntityId theirMirror = pair.Mirror(theirs);
    ASSERT_TRUE(myMirror.IsValid());
    ASSERT_TRUE(theirMirror.IsValid());
    prediction.SetPredicted(myMirror);

    pair.Authority.TryGet<LocalTransform>(theirs)->Value.Position =
        Vec3d{ 42.0f, 0.0f, 0.0f };
    pair.Replicate(0, nullptr, &prediction);

    EXPECT_NEAR(
        pair.Client.TryGet<LocalTransform>(theirMirror)->Value.Position.X, 42.0f, kWirePosition)
        << "a puppet has to be drawn where the authority says it is";
}

// What a machine derives for itself cannot travel, and does not need to: a
// contact normal is re-derived by the first replayed sweep, an asset handle is
// a slot index that means something else in another process, an aim limit is a
// property of content both ends loaded. The prefab is where those values come
// from on the receiving machine -- so this pins the chain that keeps them,
// which is not obvious and which nothing else states.
//
// The prefab is instantiated first, so the snapshot lands on an entity that
// already holds its authored values. A delta only carries the fields it names,
// so the write has to leave the rest of the component alone rather than stamp
// the staging buffer over it -- that buffer was seeded from type defaults,
// because at planning time there was no entity to seed from. The prediction
// shadow then seeds from the world's copy, which holds the prefab's value; and
// because a local-only field has no place in the wire layout at all, no mask
// bit can ever address it and no later snapshot can take it back.
//
// Break any link in that and a client silently runs on one value while the
// authority runs on another -- two simulations disagreeing for a reason no
// amount of reconciling can fix, because reconciling is what carries the
// disagreement.
TEST(ReplicationPrediction, APrefabsLocalFieldSurvivesSnapshotsAndReconcile)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);

    // A value only this machine has any business holding, which is the point:
    // it is never compared with the authority's.
    const Vec3d local(0.0f, 0.0f, 1.0f);

    constexpr AssetId kPrefab{ 0x51u };
    StubPrefabSpawner prefabs;
    prefabs.Register(kPrefab, [local](World& world, StoragePartitionId partition) {
        const EntityId entity = world.CreateEntity(partition);
        SupportState support{};
        support.Normal = local;
        world.AddComponent<SupportState>(entity, support);
        return std::vector<EntityId>{ entity };
    });

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 7 });
    SupportState authoritySupport{};
    authoritySupport.Kind = SupportKind::Stable;
    pair.Authority.AddComponent<SupportState>(authority, authoritySupport);
    pair.Authority.AddComponent<NetSpawnPrefab>(authority,
                                                NetSpawnPrefab{ .Scene = kPrefab });

    pair.Replicate(7, &prefabs);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    const SupportState* held = pair.Client.TryGet<SupportState>(mirror);
    ASSERT_NE(held, nullptr);
    ASSERT_EQ(held->Normal, local)
        << "the snapshot stamped its staging buffer over what the prefab set";
    EXPECT_EQ(held->Kind, SupportKind::Stable) << "the wire's own field did not arrive";

    // Possession happens after the spawn, which is the order that matters: the
    // shadow cannot seed before this, so it seeds from a world copy the recipe
    // has already completed.
    prediction.SetPredicted(mirror);

    pair.Authority.TryGet<SupportState>(authority)->Kind = SupportKind::Steep;
    pair.Replicate(7, &prefabs, &prediction);
    ASSERT_TRUE(pair.LastApply.ReconcilePredicted);
    EXPECT_EQ(pair.Client.TryGet<SupportState>(mirror)->Normal, local)
        << "an arriving snapshot overwrote a field it does not carry";

    // The reconcile itself: the shadow restores the whole component, so a field
    // the wire never mentions has to have been in the shadow to survive it.
    ASSERT_TRUE(prediction.RestoreTo(pair.Client, pair.Schema, mirror));
    const SupportState* restored = pair.Client.TryGet<SupportState>(mirror);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->Normal, local)
        << "the reconcile stamped a default over what the prefab set, so every "
           "replayed tick from here runs on the wrong contact";
    EXPECT_EQ(restored->Kind, SupportKind::Steep)
        << "the authority's own field did not survive the same restore";

    // And it stays put however long the session runs.
    pair.Replicate(7, &prefabs, &prediction);
    pair.Replicate(7, &prefabs, &prediction);
    EXPECT_EQ(pair.Client.TryGet<SupportState>(mirror)->Normal, local);
}

//=============================================================================
// Poses held rather than written
//
// The applier's half of interpolation. The unit tests pin what the buffer does
// with poses it is given; these pin that the applier gives it the right ones and
// keeps them out of the world in the meantime.
//=============================================================================

TEST(ReplicationInterpolationApply, AMirroredPoseIsHeldInsteadOfWritten)
{
    Pair pair;
    ReplicationInterpolation interpolation;

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate(0, nullptr, nullptr, &interpolation);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
        Vec3d{ 9.0f, 0.0f, 0.0f };
    const std::uint64_t moved = pair.Tick + 1;
    pair.Replicate(0, nullptr, nullptr, &interpolation);

    EXPECT_EQ(interpolation.TrackedCount(), 1u)
        << "the applier never handed the pose to the buffer, so nothing will "
           "present it";

    const auto held = interpolation.Resolve(mirror, moved);
    ASSERT_TRUE(held.has_value());
    EXPECT_NEAR(held->Value.Position.X, 9.0f, kWirePosition);

    // And the world still holds whatever it did: writing the arriving pose here
    // is exactly the stepping the buffer exists to replace.
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    0.0f, kWirePosition)
        << "the arriving pose was written straight to the world as well, so the "
           "entity steps to it and is then blended from it";
}

// What the world's copy is doing cannot reach the held pose. Today the transport
// of this is trivial -- a transform arrives whole or not at all -- but the
// staging source is the part that would stop being trivial the moment the
// schema splits position from rotation, and this is where that would show.
TEST(ReplicationInterpolationApply, TheHeldPoseComesFromTheWireNotTheWorld)
{
    Pair pair;
    ReplicationInterpolation interpolation;

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate(0, nullptr, nullptr, &interpolation);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    // Dragged somewhere the authority never put it, which is what presenting a
    // blend does to this component every tick.
    pair.Client.TryGet<LocalTransform>(mirror)->Value.Position =
        Vec3d{ -50.0f, -50.0f, -50.0f };

    pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
        Vec3d{ 6.0f, 0.0f, 4.0f };
    const std::uint64_t moved = pair.Tick + 1;
    pair.Replicate(0, nullptr, nullptr, &interpolation);

    const auto held = interpolation.Resolve(mirror, moved);
    ASSERT_TRUE(held.has_value());
    EXPECT_NEAR(held->Value.Position.X, 6.0f, kWirePosition);
    EXPECT_NEAR(held->Value.Position.Y, 0.0f, kWirePosition);
    EXPECT_NEAR(held->Value.Position.Z, 4.0f, kWirePosition);
}

// Prediction off does not mean the local pawn becomes a mirrored one. It is
// still simulated here -- its movement systems run either way -- so its pose has
// to land in the world as it did before prediction existed. Held back instead,
// the authority's word would have nowhere to go and the pawn would answer to
// nothing but this machine.
TEST(ReplicationInterpolationApply, TheOwnPawnIsNeverMirroredEvenWithPredictionOff)
{
    Pair pair;
    ClientPrediction prediction;
    prediction.Bind(pair.Layout);
    ReplicationInterpolation interpolation;

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate(0, nullptr, &prediction, &interpolation);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    prediction.SetPredicted(mirror);
    prediction.SetEnabled(false);

    pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
        Vec3d{ 3.0f, 0.0f, 0.0f };
    pair.Replicate(0, nullptr, &prediction, &interpolation);

    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    3.0f, kWirePosition)
        << "the pawn this machine simulates was mirrored instead of written, so "
           "the authority's pose reached neither the world nor anything that "
           "would present it";

    // A track from before adoption is expected -- a client cannot know which
    // pawn is its own until the ownership arrives, and the presenting system
    // drops it on the first tick after that. What must not happen is this
    // snapshot adding to it.
    const auto held = interpolation.Resolve(mirror, pair.Tick);
    if (held.has_value())
    {
        EXPECT_NEAR(held->Value.Position.X, 0.0f, kWirePosition)
            << "the own pawn's pose was fed to the buffer as well, so two "
               "mechanisms now hold an opinion about where it is";
    }
}

TEST(ReplicationInterpolationApply, ADestroyedEntityStopsBeingHeld)
{
    Pair pair;
    ReplicationInterpolation interpolation;

    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Replicate(0, nullptr, nullptr, &interpolation);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_EQ(interpolation.TrackedCount(), 1u);

    pair.Authority.DestroyEntity(authority);
    pair.Replicate(0, nullptr, nullptr, &interpolation);

    EXPECT_EQ(interpolation.TrackedCount(), 0u)
        << "poses kept for a destroyed entity would be inherited by whatever "
           "the handle is handed out to next";
}

//=============================================================================
// Reaching the screen
//
// The gap the first live playtest found. Every assertion above passes on an
// entity nothing will ever draw, because replication carries the authored
// transform and rendering reads the derived one. These assert the chain
// extraction actually depends on.
//
// Not the rendered frame itself: RenderExtractionSystem::Extract needs mesh and
// material caches that only exist with a device. What is asserted here is the
// component shape its queries match and the derivation that feeds them, which
// is exactly where the break was.
//=============================================================================

TEST(ReplicationVisibility, ASpawnedEntityGetsTheDerivedTransformRenderingReads)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(3.0f, 4.0f, 5.0f));
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    const WorldTransform* derived = pair.Client.TryGet<WorldTransform>(mirror);
    ASSERT_NE(derived, nullptr)
        << "a replicated entity with no world transform is invisible to "
           "extraction and to pose history, however correct its state is";
    EXPECT_NEAR(derived->Value.Position.X, 3.0f, kWirePosition);
    EXPECT_NEAR(derived->Value.Position.Y, 4.0f, kWirePosition);
    EXPECT_NEAR(derived->Value.Position.Z, 5.0f, kWirePosition);
}

// Motion, not just presence: the derived transform has to keep following the
// replicated one or the entity appears once and then freezes.
TEST(ReplicationVisibility, TheDerivedTransformFollowsReplicatedMotion)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    for (int step = 1; step <= 10; ++step)
    {
        pair.Authority.TryGet<LocalTransform>(authority)->Value.Position =
            Vec3d{ static_cast<float>(step) * 2.0f, 0.0f, 0.0f };
        pair.Replicate();

        const WorldTransform* derived = pair.Client.TryGet<WorldTransform>(mirror);
        ASSERT_NE(derived, nullptr) << "step " << step;
        ASSERT_NEAR(derived->Value.Position.X, static_cast<float>(step) * 2.0f, kWirePosition)
            << "the drawn transform stopped tracking at step " << step;
    }
}

// The recipe is what turns replicated state into something with a body. Without
// one an entity is bare, which is the state the playtest was actually in.
// A field the wire never carries is a field the sender is saying nothing
// about, not a field worth zero. LookOrientation declares its pitch limits
// local-only -- how far a thing can look is a property of the thing, identical
// on every machine that loaded it -- so they never travel, and an entity built
// from a snapshot has to get them from the type rather than from the absence.
//
// Zeroing them produces a component that decoded perfectly, validates fine, and
// pins the player's view to the horizon.
TEST(ReplicationVisibility, ASpawnedEntityKeepsTheFieldsTheWireNeverCarries)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<LookOrientation>(authority, LookOrientation{});
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    const LookOrientation* aim = pair.Client.TryGet<LookOrientation>(mirror);
    ASSERT_NE(aim, nullptr);

    const LookOrientation declared;
    EXPECT_FLOAT_EQ(aim->MinPitch, declared.MinPitch)
        << "a local-only field arrived as zero, so this player cannot look down";
    EXPECT_FLOAT_EQ(aim->MaxPitch, declared.MaxPitch)
        << "a local-only field arrived as zero, so this player cannot look up";
    EXPECT_LT(aim->MinPitch, aim->MaxPitch)
        << "an empty pitch range clamps every look to one angle";
}

// The same for the owner, whose own aim is deliberately withheld so their view
// does not fight the authority's echo of it. Withholding a field must not cost
// them the limits that field is clamped against.
TEST(ReplicationVisibility, AnOwnedSpawnKeepsItsLimitsToo)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<LookOrientation>(authority, LookOrientation{});
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 4 });
    pair.Replicate(4);

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    const LookOrientation* aim = pair.Client.TryGet<LookOrientation>(mirror);
    ASSERT_NE(aim, nullptr);
    const LookOrientation declared;
    EXPECT_FLOAT_EQ(aim->MinPitch, declared.MinPitch);
    EXPECT_FLOAT_EQ(aim->MaxPitch, declared.MaxPitch);
}

TEST(ReplicationVisibility, APrefabIsTheEntityOnArrival)
{
    Pair pair;
    constexpr AssetId kPrefab{ 0x77u };
    StubPrefabSpawner prefabs;
    int built = 0;
    prefabs.Register(kPrefab, [&built](World& world, StoragePartitionId partition) {
        ++built;
        const EntityId entity = world.CreateEntity(partition);
        world.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{});
        return std::vector<EntityId>{ entity };
    });

    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetSpawnPrefab>(authority,
                                                NetSpawnPrefab{ .Scene = kPrefab });
    pair.Replicate(0, &prefabs);

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_EQ(built, 1);
    EXPECT_TRUE(pair.Client.HasComponent<WorldTransformHistory>(mirror))
        << "the entity was created bare instead of instantiated from its prefab";
    // And the snapshot's own values landed on the instantiated root.
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 1.0f,
                kWirePosition);

    // And only once, however many snapshots follow.
    pair.Replicate(0, &prefabs);
    pair.Replicate(0, &prefabs);
    EXPECT_EQ(built, 1) << "a prefab builds an entity once, not every tick";
}

// A prefab this machine cannot build is deferred, never built bare. An entity
// with its state and no body is invisible in the literal sense: it exists, it
// is correct, and nothing draws it -- so the snapshot goes unacknowledged and
// the authority offers the entity again.
TEST(ReplicationVisibility, AnUnbuildablePrefabDefersTheSpawn)
{
    Pair pair;
    constexpr AssetId kPrefab{ 0x99u };
    StubPrefabSpawner prefabs;

    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetSpawnPrefab>(authority,
                                                NetSpawnPrefab{ .Scene = kPrefab });
    pair.Replicate(0, &prefabs);

    EXPECT_TRUE(pair.LastApply.Ok()) << "a deferral is not a decoding failure";
    EXPECT_FALSE(pair.LastApply.Complete())
        << "acknowledging would tell the authority this client holds the entity";
    EXPECT_EQ(pair.LastApply.PrefabsDeferred, 1u);
    EXPECT_FALSE(pair.Mirror(authority).IsValid())
        << "a bare entity was created for a prefab that could not be built";

    // And when the content arrives, so does the entity.
    prefabs.Register(kPrefab, [](World& world, StoragePartitionId partition) {
        const EntityId entity = world.CreateEntity(partition);
        world.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{});
        return std::vector<EntityId>{ entity };
    });
    pair.Replicate(0, &prefabs);
    EXPECT_EQ(pair.LastApply.PrefabsDeferred, 0u);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid()) << "the retry never produced the entity";
    EXPECT_TRUE(pair.Client.HasComponent<WorldTransformHistory>(mirror));
}

// The wire names one entity; a prefab may be several. Destroying the root has
// to take the group with it, or a session leaks a body's worth of entities per
// player who leaves.
TEST(ReplicationVisibility, DestroyingAPrefabRootTakesItsGroup)
{
    Pair pair;
    constexpr AssetId kPrefab{ 0xA1u };
    StubPrefabSpawner prefabs;
    EntityId child;
    prefabs.Register(kPrefab, [&child](World& world, StoragePartitionId partition) {
        const EntityId root = world.CreateEntity(partition);
        child = world.CreateEntity(partition);
        world.AddComponent<Parent>(child, Parent{ .Entity = root });
        return std::vector<EntityId>{ root, child };
    });

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetSpawnPrefab>(authority,
                                                NetSpawnPrefab{ .Scene = kPrefab });
    pair.Replicate(0, &prefabs);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_TRUE(child.IsValid());
    ASSERT_TRUE(pair.Client.IsAlive(child));

    pair.Authority.DestroyEntity(authority);
    pair.Replicate(0, &prefabs);

    EXPECT_FALSE(pair.Client.IsAlive(mirror));
    EXPECT_FALSE(pair.Client.IsAlive(child))
        << "the root's group outlived the entity the wire named";
}

//=============================================================================
// Hostile input
//
// A client applies these bytes, so a compromised authority is the threat.
//=============================================================================

TEST(ReplicationSnapshotHostile, TruncatedSnapshotsAreRefused)
{
    Pair pair;
    for (int i = 0; i < 4; ++i)
        pair.SpawnReplicated(PoseAt(static_cast<float>(i), 1.0f, 2.0f));

    SnapshotWriteRequest write;
    pair.Changes.Update(pair.Authority, pair.Layout, pair.Identity,
                        ++pair.Generation);
    write.Changes = &pair.Changes;
    write.Layout = &pair.Layout;
    write.Peer = &pair.Peer;
    write.Tick = 1;
    write.Sequence = pair.Peer.NextSnapshotSequence();
    const SnapshotWriteResult produced =
        ReplicationWriteSnapshot(write, pair.Scratch);
    ASSERT_TRUE(produced.Ok);

    for (std::size_t length = 0; length < produced.BytesWritten; ++length)
    {
        World fresh;
        pair.Schema.Apply(fresh);
        ReplicationClientIdentity identity;

        SnapshotApplyRequest apply;
        apply.Target = &fresh;
        apply.Schema = &pair.Schema;
        apply.Layout = &pair.Layout;
        apply.Identity = &identity;

        const SnapshotApplyResult result = ReplicationApplySnapshot(
            apply, std::span(pair.Scratch).subspan(0, length));
        EXPECT_FALSE(result.Ok()) << "accepted a snapshot truncated to " << length;
    }
}

TEST(ReplicationSnapshotHostile, AnUnknownComponentKeyIsRefused)
{
    Pair pair;

    // Built rather than produced-then-poked: the envelope is bit-packed, so a
    // component key does not sit on a byte boundary and there is no byte to
    // overwrite. The widths come from the wire contract, so adding a header
    // field moves this with it instead of leaving it quietly describing the old
    // shape.
    using Wire = ReplicationSnapshotWire;
    std::array<std::byte, 64> forged{};
    NetBitWriter writer(forged);
    static_assert(Wire::TickBits == 64 && Wire::CommandAckBits == 64,
                  "the two wide header fields are written as whole words");
    writer.WriteU64(1);                      // tick
    writer.WriteBits(1, Wire::SequenceBits);
    writer.WriteU64(0);                      // command acknowledgement
    writer.WriteBits(0, Wire::CountBits);    // nothing destroyed
    writer.WriteBits(1, Wire::CountBits);    // one entity
    writer.WriteVarUInt(1);                  // its identity
    writer.WriteBits(0, Wire::AuthoredPresentBits);  // not an authored entity
    writer.WriteBits(1, Wire::ComponentCountBits);
    writer.WriteBits(0xFE, Wire::ComponentIndexBits);  // a key this build lacks

    SnapshotApplyRequest apply;
    apply.Target = &pair.Client;
    apply.Schema = &pair.Schema;
    apply.Layout = &pair.Layout;
    apply.Identity = &pair.ClientIdentity;

    const SnapshotApplyResult result =
        ReplicationApplySnapshot(apply, writer.Written());
    EXPECT_EQ(result.Error, SnapshotApplyError::UnknownComponent);
}

// A claimed entity count far beyond the cap must be refused before it is used
// to loop, not discovered when the read runs out of bytes.
TEST(ReplicationSnapshotHostile, AnAbsurdEntityCountIsRefusedByTheCap)
{
    Pair pair;

    std::array<std::byte, 32> forged{};
    NetBitWriter writer(forged);
    writer.WriteU64(1);            // tick
    writer.WriteBits(1, ReplicationSnapshotWire::SequenceBits);
    writer.WriteU64(0);            // command acknowledgement
    writer.WriteBits(0, ReplicationSnapshotWire::CountBits);  // destroyed
    // Every bit the count has: more entities than the cap allows, claimed
    // before a single one has been read.
    writer.WriteBits((1u << ReplicationSnapshotWire::CountBits) - 1u,
                     ReplicationSnapshotWire::CountBits);

    SnapshotApplyRequest apply;
    apply.Target = &pair.Client;
    apply.Schema = &pair.Schema;
    apply.Layout = &pair.Layout;
    apply.Identity = &pair.ClientIdentity;

    const SnapshotApplyResult result =
        ReplicationApplySnapshot(apply, writer.Written());
    EXPECT_EQ(result.Error, SnapshotApplyError::CapExceeded);
    EXPECT_EQ(pair.ClientIdentity.Size(), 0u);
}

TEST(ReplicationSnapshotHostile, RandomBytesNeverCorruptTheClientWorld)
{
    Pair pair;
    std::uint32_t seed = 0xC0FFEEu;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::byte>((seed >> 16) & 0xFF);
    };

    for (int trial = 0; trial < 500; ++trial)
    {
        std::array<std::byte, 96> noise{};
        for (std::byte& byte : noise)
            byte = next();

        SnapshotApplyRequest apply;
        apply.Target = &pair.Client;
        apply.Schema = &pair.Schema;
        apply.Layout = &pair.Layout;
        apply.Identity = &pair.ClientIdentity;

        // Whatever it decides, it must not crash, and every transform it
        // produced has to be a number.
        const SnapshotApplyResult result = ReplicationApplySnapshot(apply, noise);
        (void)result;

        for (const auto& [id, entity] : pair.ClientIdentity.All())
        {
            if (!pair.Client.IsAlive(entity))
                continue;
            if (const LocalTransform* t = pair.Client.TryGet<LocalTransform>(entity))
            {
                ASSERT_FALSE(std::isnan(t->Value.Position.X)) << "trial " << trial;
                ASSERT_FALSE(std::isnan(t->Value.Position.Y)) << "trial " << trial;
                ASSERT_FALSE(std::isnan(t->Value.Position.Z)) << "trial " << trial;
            }
        }
    }
}


//=============================================================================
// Filling one datagram
//
// A snapshot rides the unreliable channel, so it has to fit one datagram, and a
// world can be bigger than that. What the authority does about it is the whole
// difference between a session that scales and one that stops working at a
// couple of dozen entities: writing nothing at all sounds like the conservative
// answer and is the opposite, because the peer that needs the largest snapshot
// is the one that has just joined and been told nothing.
//=============================================================================

namespace
{
    // A budget that holds exactly `count` freshly spawned entities, measured
    // rather than written down. What an entity costs on the wire is a property
    // of the schema and the envelope, and both of those move: a byte count in a
    // test stops meaning what it says the moment either does, and a test whose
    // premise has quietly stopped holding still passes.
    std::size_t BudgetForEntities(std::size_t count)
    {
        Pair sizing;
        for (std::size_t i = 0; i < count; ++i)
            (void)sizing.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f));
        return sizing.Replicate();
    }

    // Small enough that a modest world does not fit, large enough that the fill
    // has a real packing decision to make rather than a trivial one.
    std::size_t TightBudget() { return BudgetForEntities(4); }
}

// The one this mechanism exists for. Every entity is owed to a peer that has
// confirmed nothing, so the snapshot that would seed it is the largest it will
// ever be sent; an authority that gives up on a snapshot it cannot fit never
// sends that peer anything, and a peer that receives nothing is never owed any
// less. It is not a slow join, it is a permanent one.
TEST(ReplicationBudget, APeerJoiningAWorldLargerThanOneDatagramIsSeededAnyway)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;

    constexpr std::size_t kEntities = 40;
    std::vector<EntityId> world;
    for (std::size_t i = 0; i < kEntities; ++i)
        world.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f)));

    std::vector<std::size_t> sizes{ pair.Replicate() };
    ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u)
        << "the budget never bit, so nothing below is a test of it";
    ASSERT_LE(sizes.front(), budget);

    while (pair.ClientIdentity.Size() < kEntities && sizes.size() < kEntities * 2)
        sizes.push_back(pair.Replicate());

    ASSERT_EQ(pair.ClientIdentity.Size(), kEntities)
        << "a peer that could not be told everything at once was never told the rest";
    for (std::size_t i = 0; i < kEntities; ++i)
    {
        const EntityId mirror = pair.Mirror(world[i]);
        ASSERT_TRUE(mirror.IsValid()) << "entity " << i;
        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr) << "entity " << i;
        EXPECT_NEAR(seen->Value.Position.X, static_cast<float>(i), kWirePosition);
    }

    // Packed rather than dribbled. One entity per snapshot would arrive here
    // too, eventually, and would make a join take as many round trips as the
    // world has entities.
    EXPECT_LT(sizes.size(), kEntities)
        << "seeding cost a snapshot per entity";
    for (std::size_t i = 0; i + 1 < sizes.size(); ++i)
    {
        EXPECT_GT(sizes[i], budget / 2)
            << "publish " << i << " left half the datagram empty";
    }
}

// Deferring is not describing. An entity left out for room must stay owed in
// full: if making room advanced what the peer is believed to hold, the state it
// was holding a place for would be measured as already delivered and never sent
// at all.
TEST(ReplicationBudget, WhatDidNotFitIsStillOwedInFull)
{
    Pair pair;
    // One entity's worth. The second has nowhere to go.
    pair.Budget = BudgetForEntities(1);

    const EntityId first = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    const EntityId second = pair.SpawnReplicated(PoseAt(2.0f, 0.0f, 0.0f));

    pair.Replicate();
    ASSERT_EQ(pair.LastWrite.EntitiesWritten, 1u);
    ASSERT_EQ(pair.LastWrite.EntitiesDeferred, 1u);

    const NetEntityId deferred = pair.Identity.TryFind(second);
    ASSERT_TRUE(deferred.IsValid());
    EXPECT_EQ(pair.Peer.Floor(deferred), 0u)
        << "the peer was credited with state that was never written";
    EXPECT_EQ(pair.Peer.LastSentAt(deferred), 0u)
        << "an entity that was left out was recorded as having had its turn";

    // It is left exactly where it was, which is the case a credited floor loses
    // entirely: a peer believed to hold state it was never sent is owed nothing
    // further about it, and nothing about an entity at rest ever moves again to
    // put it back in the queue. It would simply never arrive.
    pair.Replicate();

    EntityId mirror = pair.Mirror(second);
    ASSERT_TRUE(mirror.IsValid()) << "the entity that waited its turn never came";
    const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
    ASSERT_NE(seen, nullptr);
    EXPECT_NEAR(seen->Value.Position.X, 2.0f, kWirePosition);

    // And once it has genuinely been received, it keeps up like anything else.
    pair.Authority.TryGet<LocalTransform>(second)->Value.Position =
        Vec3d{ 9.0f, 0.0f, 0.0f };
    pair.Replicate();
    mirror = pair.Mirror(second);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_NEAR(
        pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 9.0f, kWirePosition);

    // And the one that did fit was not disturbed by any of it.
    const EntityId firstMirror = pair.Mirror(first);
    ASSERT_TRUE(firstMirror.IsValid());
    EXPECT_NEAR(
        pair.Client.TryGet<LocalTransform>(firstMirror)->Value.Position.X, 1.0f, kWirePosition);
}

// Everything moves every publish and a quarter of it fits. Whichever entities
// lost their place last time have to win it this time, or a busy world means a
// fixed few entities are the only ones anybody ever sees move -- and which few
// would be decided by something as arbitrary as the order they were created in.
TEST(ReplicationBudget, ABusyWorldRotatesInsteadOfFavouringTheSameFewEntities)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;

    constexpr std::size_t kEntities = 20;
    std::vector<EntityId> world;
    for (std::size_t i = 0; i < kEntities; ++i)
        world.push_back(pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f)));

    // Seeded first, so what follows measures the fill order under pressure
    // rather than the join.
    while (pair.ClientIdentity.Size() < kEntities)
        pair.Replicate();

    // Now nothing is at rest: every entity has something new to say on every
    // publish, and they cannot all be accommodated.
    for (std::size_t publish = 1; publish <= kEntities; ++publish)
    {
        for (std::size_t i = 0; i < kEntities; ++i)
        {
            pair.Authority.TryGet<LocalTransform>(world[i])->Value.Position =
                Vec3d{ static_cast<float>(publish), static_cast<float>(i), 0.0f };
        }
        pair.Replicate();
        ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u)
            << "publish " << publish << " fitted everything, so there was no "
               "contention to resolve";
    }

    for (std::size_t i = 0; i < kEntities; ++i)
    {
        const EntityId mirror = pair.Mirror(world[i]);
        ASSERT_TRUE(mirror.IsValid()) << "entity " << i;
        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr) << "entity " << i;
        EXPECT_GT(seen->Value.Position.X, 0.0f)
            << "entity " << i << " never once won a place in a snapshot";
    }
}

// A budget that cannot hold one particular entity, which no amount of patience
// fixes. It must not take the snapshot down with it, must not block the entities
// queued behind it, and must be counted: an entity nothing can be said about is
// a fault for an operator to see rather than one to discover as a missing
// object.
TEST(ReplicationBudget, AnEntityTooLargeForAnySnapshotIsCountedNotSilentlyDropped)
{
    Pair pair;
    // One byte short of what a single entity carrying a transform needs, which
    // still leaves room for one carrying no replicated components at all.
    pair.Budget = BudgetForEntities(1) - 1;

    const EntityId heavy = pair.SpawnReplicated(PoseAt(4.0f, 0.0f, 0.0f));
    const EntityId bare = pair.Authority.CreateEntity();
    pair.Authority.AddComponent<NetReplicated>(bare);
    // Identity is minted here rather than left to the order the store happens to
    // walk the chunks in, so the one that cannot fit is reliably first in the
    // queue. That is the arrangement that matters: an entity nothing can be done
    // about has to be stepped over, not allowed to stop the fill where it stands.
    const NetEntityId heavyId = pair.Identity.IdFor(heavy);
    const NetEntityId bareId = pair.Identity.IdFor(bare);
    ASSERT_LT(heavyId.Value, bareId.Value);

    pair.Replicate();

    EXPECT_TRUE(pair.LastWrite.Ok)
        << "one entity nobody can describe stopped the whole snapshot";
    EXPECT_EQ(pair.LastWrite.EntitiesUnsendable, 1u);
    EXPECT_EQ(pair.LastWrite.EntitiesDeferred, 0u)
        << "an entity that will never fit was counted as merely waiting";
    EXPECT_EQ(pair.LastWrite.EntitiesWritten, 1u);
    EXPECT_TRUE(pair.Mirror(bare).IsValid())
        << "an entity that fitted was held up behind one that never will";
    EXPECT_FALSE(pair.Mirror(heavy).IsValid());
}

// An update that waits is superseded by the next one, so waiting costs it
// nothing but freshness. A destroy that waits is superseded by nothing: the
// authority has no further reason to mention an entity it no longer has, so a
// client that missed the news keeps the thing forever.
TEST(ReplicationBudget, ADestroyIsNotHeldBehindASnapshotFullOfUpdates)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;

    constexpr std::size_t kEntities = 20;
    std::vector<EntityId> world;
    for (std::size_t i = 0; i < kEntities; ++i)
        world.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f)));
    while (pair.ClientIdentity.Size() < kEntities)
        pair.Replicate();

    const EntityId doomed = world.front();
    const EntityId mirror = pair.Mirror(doomed);
    ASSERT_TRUE(mirror.IsValid());

    // The rest of the world is busy enough to fill the budget on its own.
    for (std::size_t i = 1; i < kEntities; ++i)
    {
        pair.Authority.TryGet<LocalTransform>(world[i])->Value.Position =
            Vec3d{ 100.0f, 0.0f, 0.0f };
    }
    pair.Authority.DestroyEntity(doomed);

    pair.Replicate();

    EXPECT_EQ(pair.LastWrite.EntitiesDestroyed, 1u);
    EXPECT_EQ(pair.LastWrite.DestroysDeferred, 0u);
    ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u)
        << "the snapshot had room to spare, so the destroy was not competing "
           "with anything";
    EXPECT_FALSE(pair.Client.IsAlive(mirror))
        << "the client is still holding an entity the world destroyed";
}

// A peer's own entity is not one of the crowd. Everything else it receives is
// mirrored and presented a fixed distance in the past, so a late sample is
// smoothed over and nobody sees it; its own entity is what its prediction argues
// with, and one that arrives late means it keeps predicting uncorrected until
// the correction is large enough to see as a jump. Ordering by anything but
// ownership would leave that to whichever entity happened to be created first.
TEST(ReplicationBudget, APeersOwnEntityIsCarriedWhileTheWorldSaturatesTheBudget)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;
    constexpr std::uint32_t kOwner = 3;

    constexpr std::size_t kCrowd = 40;
    std::vector<EntityId> crowd;
    for (std::size_t i = 0; i < kCrowd; ++i)
        crowd.push_back(pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f)));

    // Created last, so identity order works against it and nothing but
    // ownership can put it at the front of the queue.
    const EntityId pawn = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = kOwner });

    for (std::size_t publish = 1; publish <= 12; ++publish)
    {
        const auto moved = static_cast<float>(publish);
        pair.Authority.TryGet<LocalTransform>(pawn)->Value.Position =
            Vec3d{ moved, 0.0f, 0.0f };
        for (EntityId other : crowd)
        {
            pair.Authority.TryGet<LocalTransform>(other)->Value.Position =
                Vec3d{ 0.0f, moved, 0.0f };
        }

        pair.Replicate(kOwner);
        ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u)
            << "publish " << publish << " had room for everyone, so nothing was "
               "competing with the pawn";

        const EntityId mirror = pair.Mirror(pawn);
        ASSERT_TRUE(mirror.IsValid())
            << "publish " << publish << ": the owner has not been told about its "
               "own entity yet";
        const LocalTransform* seen = pair.Client.TryGet<LocalTransform>(mirror);
        ASSERT_NE(seen, nullptr);
        EXPECT_NEAR(seen->Value.Position.X, moved, kWirePosition)
            << "publish " << publish << ": the owner is a snapshot behind on its "
               "own entity";
    }
}

// Emptying a zone is a burst of destroys that no later snapshot repeats, so they
// go early -- but not so early that a player pays for it. The snapshot keeps
// room for the peer's own entity ahead of them.
TEST(ReplicationBudget, AMassDestroyDoesNotCostAPlayerTheirOwnState)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;
    constexpr std::uint32_t kOwner = 5;

    constexpr std::size_t kCrowd = 60;
    std::vector<EntityId> crowd;
    for (std::size_t i = 0; i < kCrowd; ++i)
        crowd.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f)));
    const EntityId pawn = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(pawn, NetOwner{ .Peer = kOwner });

    while (pair.ClientIdentity.Size() < kCrowd + 1)
        pair.Replicate(kOwner);

    // The zone goes, and the player moves in the same tick.
    for (EntityId doomed : crowd)
        pair.Authority.DestroyEntity(doomed);
    pair.Authority.TryGet<LocalTransform>(pawn)->Value.Position =
        Vec3d{ 42.0f, 0.0f, 0.0f };

    pair.Replicate(kOwner);

    ASSERT_GT(pair.LastWrite.EntitiesDestroyed, 0u);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    42.0f, kWirePosition)
        << "the player's own state waited behind the news that a zone emptied";
}

// The debt is paced, not dropped. A destroy stays owed until the peer confirms
// it, so capping how many ride in one snapshot spends the burst over several
// rather than handing one snapshot entirely to entities that no longer exist.
TEST(ReplicationBudget, AMassDestroyIsPacedRatherThanDropped)
{
    Pair pair;
    constexpr std::size_t kEntities = 100;
    std::vector<EntityId> world;
    for (std::size_t i = 0; i < kEntities; ++i)
        world.push_back(pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f)));
    pair.Replicate();
    ASSERT_EQ(pair.ClientIdentity.Size(), kEntities);

    std::vector<EntityId> mirrors;
    for (EntityId entity : world)
        mirrors.push_back(pair.Mirror(entity));

    for (EntityId entity : world)
        pair.Authority.DestroyEntity(entity);

    pair.Replicate();
    EXPECT_GT(pair.LastWrite.DestroysDeferred, 0u)
        << "a hundred destroys rode one snapshot, so nothing is being paced";

    for (int publish = 0; publish < 20; ++publish)
        pair.Replicate();

    for (std::size_t i = 0; i < mirrors.size(); ++i)
    {
        EXPECT_FALSE(pair.Client.IsAlive(mirrors[i]))
            << "entity " << i << " outlived the world by more than the pacing";
    }
}

// A spawn is news a player is waiting on -- a rocket that appears half a second
// late has already hit them. Whatever the load, an entity a peer has never been
// sent goes ahead of everything it already holds.
TEST(ReplicationBudget, ANewEntityIsNotQueuedBehindOnesThePeerAlreadyHas)
{
    Pair pair;
    const std::size_t budget = TightBudget();
    pair.Budget = budget;

    constexpr std::size_t kCrowd = 30;
    std::vector<EntityId> crowd;
    for (std::size_t i = 0; i < kCrowd; ++i)
        crowd.push_back(pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f)));
    while (pair.ClientIdentity.Size() < kCrowd)
        pair.Replicate();

    // Everything the peer already has is moving, so the queue is full before the
    // new entity is even created.
    for (EntityId entity : crowd)
    {
        pair.Authority.TryGet<LocalTransform>(entity)->Value.Position =
            Vec3d{ 7.0f, 0.0f, 0.0f };
    }
    const EntityId arrival = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 11.0f));

    pair.Replicate();

    ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u)
        << "the snapshot had room for everything, so nothing was queued";
    const EntityId mirror = pair.Mirror(arrival);
    ASSERT_TRUE(mirror.IsValid()) << "the spawn waited behind entities the peer "
                                     "already had";
    EXPECT_NEAR(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.Z, 11.0f, kWirePosition);
}

// Which entities a full snapshot takes is decided by their identity, not by
// which chunk they happen to sit in. Storage order moves when an entity gains or
// loses a component, and a fill that followed it would send a different set for
// reasons nothing in the simulation can see -- the same session replayed would
// not produce the same stream, and the divergence would be attributed to
// whatever was actually being investigated.
TEST(ReplicationBudget, WhoGoesFirstFollowsIdentityRatherThanStorageOrder)
{
    Pair pair;
    // Room for a handful, so most of the world is left for later snapshots.
    const std::size_t budget = TightBudget();
    pair.Budget = budget;

    constexpr std::size_t kEntities = 40;
    std::vector<EntityId> world;
    for (std::size_t i = 0; i < kEntities; ++i)
    {
        const EntityId entity = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
        // Spread across archetypes so that identity order and chunk order
        // genuinely disagree: every other entity lives in a different chunk.
        if (i % 2 == 0)
            pair.Authority.AddComponent<LookOrientation>(entity);
        world.push_back(entity);
        // Minted in creation order, so identity is a fact about the entity
        // rather than about which chunk the store reached first.
        (void)pair.Identity.IdFor(entity);
    }

    pair.Replicate();
    ASSERT_GT(pair.LastWrite.EntitiesDeferred, 0u);

    // Whatever fitted, it is a prefix of the identities -- not a scattering of
    // whichever chunks came first.
    const std::size_t carried = pair.LastWrite.EntitiesWritten;
    ASSERT_GT(carried, 0u);
    for (std::size_t i = 0; i < kEntities; ++i)
    {
        const bool arrived = pair.Mirror(world[i]).IsValid();
        EXPECT_EQ(arrived, i < carried)
            << "entity " << i << (arrived ? " arrived out of turn"
                                          : " was passed over in its turn");
    }
}

// Two runs of the same simulation have to produce the same bytes. Under a budget
// the writer is choosing who goes and who waits on every publish, and a session
// that cannot be replayed byte for byte is one where a divergence cannot be
// attributed. This pins the whole pipeline's reproducibility within a build; the
// test above is what pins the ordering rule it rests on.
TEST(ReplicationBudget, TheSameSimulationProducesTheSameStreamTwice)
{
    const auto run = [] {
        Pair pair;
        const std::size_t budget = TightBudget();
    pair.Budget = budget;
        std::vector<std::vector<std::byte>> stream;

        std::vector<EntityId> world;
        for (std::size_t i = 0; i < 30; ++i)
        {
            const EntityId entity =
                pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f));
            // Spread across archetypes, so storage order and identity order
            // genuinely disagree.
            if (i % 3 == 0)
                pair.Authority.AddComponent<LookOrientation>(entity);
            world.push_back(entity);
        }

        for (std::size_t publish = 1; publish <= 15; ++publish)
        {
            for (std::size_t i = 0; i < world.size(); ++i)
            {
                pair.Authority.TryGet<LocalTransform>(world[i])->Value.Position =
                    Vec3d{ static_cast<float>(publish), static_cast<float>(i), 0.0f };
            }
            // A death part way through, so the destroy records take part in the
            // comparison rather than only the updates.
            if (publish == 8)
            {
                pair.Authority.DestroyEntity(world[4]);
                world.erase(world.begin() + 4);
            }

            const std::size_t bytes = pair.Replicate();
            stream.emplace_back(pair.Scratch.begin(),
                                pair.Scratch.begin() + static_cast<std::ptrdiff_t>(bytes));
        }
        return stream;
    };

    const std::vector<std::vector<std::byte>> first = run();
    const std::vector<std::vector<std::byte>> second = run();

    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        EXPECT_EQ(first[i], second[i]) << "snapshot " << i << " differed";
}

//=============================================================================
// A refused snapshot changes nothing
//
// The applier reads a snapshot whole before it writes any of it, so a
// truncation or a corruption anywhere in the body is discovered with the world
// and the identity map untouched. That is the property the two passes exist
// for, and it is checked by mutilating a snapshot known to be good and then
// comparing the client against what it was.
//
// Each case gets a fixture of its own. A snapshot that survives being mutilated
// is still a snapshot: it applies, and it leaves a client that is no longer the
// one the next case is supposed to start from.
//=============================================================================

namespace
{
    // Everything an apply is allowed to change, in a form two of which can be
    // compared. Keyed by the identity that named each entity, because a local
    // handle only means something beside one.
    struct ClientState
    {
        struct Entity
        {
            std::uint64_t Id = 0;
            bool Alive = false;
            std::vector<std::pair<std::uint32_t, std::vector<std::byte>>> Components;

            bool operator==(const Entity&) const = default;
        };
        std::vector<Entity> Entities;

        bool operator==(const ClientState&) const = default;
    };

    ClientState CaptureClient(const Pair& pair)
    {
        ClientState state;
        for (const auto& [id, entity] : pair.ClientIdentity.All())
        {
            ClientState::Entity captured;
            captured.Id = id.Value;
            captured.Alive = pair.Client.IsAlive(entity);
            if (captured.Alive)
            {
                for (const ReplicatedComponent& component : pair.Layout.Components())
                {
                    if (!pair.Client.IsRegistered(component.Type))
                        continue;
                    const ComponentId column =
                        pair.Client.GetComponentIdByType(component.Type);
                    if (!pair.Client.HasComponent(entity, column))
                        continue;
                    const auto* bytes = static_cast<const std::byte*>(
                        pair.Client.GetComponentRaw(entity, column));
                    captured.Components.emplace_back(
                        component.Type.Value,
                        std::vector<std::byte>(bytes, bytes + component.Size));
                }
            }
            state.Entities.push_back(std::move(captured));
        }
        std::sort(state.Entities.begin(), state.Entities.end(),
                  [](const ClientState::Entity& a, const ClientState::Entity& b) {
                      return a.Id < b.Id;
                  });
        return state;
    }

    // Applies bytes without the harness's own expectations, because these tests
    // are about snapshots that are meant to be refused.
    SnapshotApplyResult ApplyRaw(Pair& pair, std::span<const std::byte> bytes,
                                 ReplicationInterpolation* interpolation = nullptr)
    {
        SnapshotApplyRequest apply;
        apply.Target = &pair.Client;
        apply.Schema = &pair.Schema;
        apply.Layout = &pair.Layout;
        apply.Identity = &pair.ClientIdentity;
        apply.Interpolation = interpolation;
        return ReplicationApplySnapshot(apply, bytes);
    }

    // A client holding several entities, and one more snapshot describing
    // updates and a spawn together -- so a partial apply would have something
    // to damage. Deterministic, so every fixture reaches the same state.
    std::vector<std::byte> BusySnapshot(Pair& pair)
    {
        std::vector<EntityId> spawned;
        for (int i = 0; i < 4; ++i)
            spawned.push_back(pair.SpawnReplicated(PoseAt(float(i), 0.0f, 0.0f)));
        pair.Replicate();

        for (const EntityId entity : spawned)
        {
            if (LocalTransform* pose = pair.Authority.TryGet<LocalTransform>(entity))
                pose->Value.Position.X += 1.0f;
        }
        (void)pair.SpawnReplicated(PoseAt(9.0f, 9.0f, 9.0f));

        ++pair.Tick;
        pair.Changes.Update(pair.Authority, pair.Layout, pair.Identity,
                            ++pair.Generation);
        SnapshotWriteRequest write;
        write.Changes = &pair.Changes;
        write.Layout = &pair.Layout;
        write.Peer = &pair.Peer;
        write.Tick = pair.Tick;
        write.Sequence = pair.Peer.NextSnapshotSequence();
        const SnapshotWriteResult result = ReplicationWriteSnapshot(
            write, std::span(pair.Scratch).subspan(0, pair.Budget));
        EXPECT_TRUE(result.Ok);
        return std::vector<std::byte>(pair.Scratch.begin(),
                                      pair.Scratch.begin() + result.BytesWritten);
    }
}

TEST(ReplicationSnapshotAtomicity, NoTruncationLeavesTheWorldPartlyChanged)
{
    Pair reference;
    const std::vector<std::byte> whole = BusySnapshot(reference);
    ASSERT_GT(whole.size(), 8u);
    const ClientState before = CaptureClient(reference);

    for (std::size_t cut = 0; cut < whole.size(); ++cut)
    {
        Pair pair;
        const std::vector<std::byte> again = BusySnapshot(pair);
        ASSERT_EQ(again, whole) << "the fixture is not reproducible";
        ASSERT_EQ(CaptureClient(pair), before);

        const SnapshotApplyResult applied =
            ApplyRaw(pair, std::span(whole).subspan(0, cut));
        if (applied.Ok())
            continue;  // a prefix that happened to be a whole smaller snapshot

        EXPECT_EQ(CaptureClient(pair), before)
            << "truncating at byte " << cut << " changed the client anyway";
    }
}

// The pose buffer is state a refused apply can damage as surely as the world is,
// and it is easier to damage: a mirrored pose is staged against what the
// authority last said, so reading the shadow at all used to be enough to begin
// tracking an entity the snapshot then turned out not to describe. Tracking is
// reachable through Commit alone now, which runs only in the pass that cannot
// fail -- this is that shape asserted rather than assumed.
TEST(ReplicationSnapshotAtomicity, NoTruncationLeavesAPoseTrackBehind)
{
    Pair reference;
    const std::vector<std::byte> whole = BusySnapshot(reference);
    ASSERT_GT(whole.size(), 8u);

    for (std::size_t cut = 0; cut < whole.size(); ++cut)
    {
        Pair pair;
        const std::vector<std::byte> again = BusySnapshot(pair);
        ASSERT_EQ(again, whole) << "the fixture is not reproducible";

        ReplicationInterpolation interpolation;
        ASSERT_EQ(interpolation.TrackedCount(), 0u);

        const SnapshotApplyResult applied =
            ApplyRaw(pair, std::span(whole).subspan(0, cut), &interpolation);
        if (applied.Ok())
            continue;  // a prefix that happened to be a whole smaller snapshot

        EXPECT_EQ(interpolation.TrackedCount(), 0u)
            << "truncating at byte " << cut << " left a track for an entity the "
               "snapshot never went on to describe";
    }
}

TEST(ReplicationSnapshotAtomicity, NoMutationLeavesTheWorldPartlyChanged)
{
    Pair reference;
    const std::vector<std::byte> whole = BusySnapshot(reference);
    const ClientState before = CaptureClient(reference);

    for (std::size_t i = 0; i < whole.size(); ++i)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            std::vector<std::byte> mutated = whole;
            mutated[i] ^= static_cast<std::byte>(1u << bit);

            Pair pair;
            (void)BusySnapshot(pair);

            const SnapshotApplyResult applied = ApplyRaw(pair, mutated);
            if (applied.Ok())
                continue;  // a flip the format tolerates; it described some
                           // other valid world, which is not this test's business

            EXPECT_EQ(CaptureClient(pair), before)
                << "flipping bit " << bit << " of byte " << i
                << " changed the client anyway";
        }
    }
}

//=============================================================================
// A component that goes away
//
// Removing a component is an ordinary thing to do to an entity, and until the
// wire could say it, doing so on a replicated entity meant every client that
// had been shown the component kept it -- and, worse, a client joining later
// was sent it, because a fresh floor owes whatever the store still holds.
//=============================================================================

TEST(ReplicationRemoval, ARemovedComponentLeavesTheClient)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 3 });
    pair.Replicate(3);

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    ASSERT_NE(pair.Client.TryGet<NetOwner>(mirror), nullptr);

    pair.Authority.RemoveComponent<NetOwner>(authority);
    pair.Replicate(3);

    EXPECT_EQ(pair.LastApply.ComponentsRemoved, 1u);
    EXPECT_EQ(pair.Client.TryGet<NetOwner>(mirror), nullptr)
        << "the client kept a component the authority no longer has";
    EXPECT_NE(pair.Client.TryGet<LocalTransform>(mirror), nullptr)
        << "removing one component took another with it";
}

// The case that made this worth building rather than documenting. A peer that
// joins after the removal has a floor of zero, so everything the store holds is
// owed to it -- and what the store held was the component's last bytes.
TEST(ReplicationRemoval, APeerJoiningAfterwardsIsNeverSentIt)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(2.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 1 });
    pair.Replicate(1);
    pair.Authority.RemoveComponent<NetOwner>(authority);
    pair.Replicate(1);

    // A second client, hearing about this world for the first time.
    World latecomer;
    pair.Schema.Apply(latecomer);
    ReplicationClientIdentity latecomerIdentity;
    ReplicationPeerState fresh;

    ++pair.Tick;
    pair.Changes.Update(pair.Authority, pair.Layout, pair.Identity, ++pair.Generation);
    SnapshotWriteRequest write;
    write.Changes = &pair.Changes;
    write.Layout = &pair.Layout;
    write.Peer = &fresh;
    write.Tick = pair.Tick;
    write.Sequence = fresh.NextSnapshotSequence();
    const SnapshotWriteResult written = ReplicationWriteSnapshot(
        write, std::span(pair.Scratch).subspan(0, pair.Budget));
    ASSERT_TRUE(written.Ok);

    SnapshotApplyRequest apply;
    apply.Target = &latecomer;
    apply.Schema = &pair.Schema;
    apply.Layout = &pair.Layout;
    apply.Identity = &latecomerIdentity;
    const SnapshotApplyResult applied = ReplicationApplySnapshot(
        apply, std::span(pair.Scratch).subspan(0, written.BytesWritten));
    ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);

    const NetEntityId id = pair.Identity.TryFind(authority);
    const EntityId seeded = latecomerIdentity.TryResolve(id);
    ASSERT_TRUE(seeded.IsValid());
    EXPECT_EQ(latecomer.TryGet<NetOwner>(seeded), nullptr)
        << "a peer that never saw the component was sent it anyway";
    EXPECT_NE(latecomer.TryGet<LocalTransform>(seeded), nullptr);
}

// Removals are owed until confirmed, exactly as destroys are: a lost snapshot
// must not be the only time the news travelled.
TEST(ReplicationRemoval, ARemovalIsResentUntilThePeerConfirmsIt)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(3.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 2 });
    pair.Replicate(2);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_NE(pair.Client.TryGet<NetOwner>(mirror), nullptr);

    // The snapshot carrying the removal never arrives.
    pair.Authority.RemoveComponent<NetOwner>(authority);
    pair.Deliver = false;
    pair.Replicate(2);
    pair.Deliver = true;
    EXPECT_NE(pair.Client.TryGet<NetOwner>(mirror), nullptr);

    pair.Replicate(2);
    EXPECT_EQ(pair.Client.TryGet<NetOwner>(mirror), nullptr)
        << "the removal was said once, into a snapshot that was lost";
}

TEST(ReplicationRemoval, RemovingAndAddingBackConverges)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(4.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 5 });
    pair.Replicate(5);

    pair.Authority.RemoveComponent<NetOwner>(authority);
    pair.Replicate(5);
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_EQ(pair.Client.TryGet<NetOwner>(mirror), nullptr);

    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 9 });
    pair.Replicate(9);

    const NetOwner* owner = pair.Client.TryGet<NetOwner>(mirror);
    ASSERT_NE(owner, nullptr) << "the component came back and the client missed it";
    EXPECT_EQ(owner->Peer, 9u);
}

// A removal for something the client never had describes a state it is already
// in, so it is not an error and not counted.
TEST(ReplicationRemoval, ARemovalForAComponentNeverHeldIsNotAnError)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(5.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetOwner>(authority, NetOwner{ .Peer = 6 });
    pair.Replicate(6);

    // Removed before the peer below is ever told the entity exists.
    pair.Authority.RemoveComponent<NetOwner>(authority);

    World latecomer;
    pair.Schema.Apply(latecomer);
    ReplicationClientIdentity latecomerIdentity;
    ReplicationPeerState fresh;

    ++pair.Tick;
    pair.Changes.Update(pair.Authority, pair.Layout, pair.Identity, ++pair.Generation);
    SnapshotWriteRequest write;
    write.Changes = &pair.Changes;
    write.Layout = &pair.Layout;
    write.Peer = &fresh;
    write.Tick = pair.Tick;
    write.Sequence = fresh.NextSnapshotSequence();
    const SnapshotWriteResult written = ReplicationWriteSnapshot(
        write, std::span(pair.Scratch).subspan(0, pair.Budget));
    ASSERT_TRUE(written.Ok);

    SnapshotApplyRequest apply;
    apply.Target = &latecomer;
    apply.Schema = &pair.Schema;
    apply.Layout = &pair.Layout;
    apply.Identity = &latecomerIdentity;
    const SnapshotApplyResult applied = ReplicationApplySnapshot(
        apply, std::span(pair.Scratch).subspan(0, written.BytesWritten));
    EXPECT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
    EXPECT_EQ(applied.ComponentsRemoved, 0u)
        << "counted a removal of something that was never there";
}

//=============================================================================
// Authored entities
//
// A door in a level exists on both machines because both loaded the level, not
// because one told the other. Their runtime handles differ and their authored
// identities do not, so a snapshot that says nothing about the authored one
// leaves a client building a second door beside the one it already had --
// bare, in the wrong partition, and indistinguishable to everything holding a
// handle on either.
//=============================================================================

namespace
{
    // Puts an entity in a world under an authored identity, the way a level
    // load does: the component registers it in the index through its own
    // lifecycle hook.
    EntityId Author(World& world, PersistentEntityId id, const Transform3f& pose)
    {
        const EntityId entity = world.CreateEntity();
        world.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ id });
        world.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
        return entity;
    }
}

TEST(ReplicationAuthored, AClientRecognisesItsOwnCopyInsteadOfBuildingASecond)
{
    Pair pair;
    pair.Authority.AddResource<PersistentEntityIndex>();
    pair.Client.AddResource<PersistentEntityIndex>();

    const PersistentEntityId door{ 4242 };
    const EntityId authority = Author(pair.Authority, door, PoseAt(3.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetReplicated>(authority);
    // The client loaded the same level, so it already has one.
    const EntityId local = Author(pair.Client, door, PoseAt(3.0f, 0.0f, 0.0f));

    pair.Replicate();

    EXPECT_EQ(pair.LastApply.AuthoredBound, 1u);
    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 0u)
        << "the client built a second copy of an entity it already had";
    EXPECT_EQ(pair.Mirror(authority), local)
        << "the wire identity did not land on the client's own copy";
    EXPECT_NE(pair.Client.TryGet<PersistentIdComponent>(local), nullptr)
        << "binding took the authored identity off the entity";
}

TEST(ReplicationAuthored, AuthoredStateFollowsOntoTheClientsOwnCopy)
{
    Pair pair;
    pair.Authority.AddResource<PersistentEntityIndex>();
    pair.Client.AddResource<PersistentEntityIndex>();

    const PersistentEntityId lift{ 77 };
    const EntityId authority = Author(pair.Authority, lift, PoseAt(0.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetReplicated>(authority);
    const EntityId local = Author(pair.Client, lift, PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();

    if (LocalTransform* pose = pair.Authority.TryGet<LocalTransform>(authority))
        pose->Value.Position.Y += 4.0f;
    pair.Replicate();

    const LocalTransform* moved = pair.Client.TryGet<LocalTransform>(local);
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->Value.Position.Y, 4.0f, kWirePosition);
}

// The client is admitted before it loads the map, so a snapshot naming an
// authored entity can arrive first. Building one then is the duplicate this
// exists to prevent, so the record is read and dropped -- and because nothing
// about it is recorded as confirmed, it comes back.
TEST(ReplicationAuthored, AnEntityNamedBeforeTheLevelLoadsIsDeferredNotDuplicated)
{
    Pair pair;
    pair.Authority.AddResource<PersistentEntityIndex>();
    pair.Client.AddResource<PersistentEntityIndex>();

    const PersistentEntityId gate{ 9001 };
    const EntityId authority = Author(pair.Authority, gate, PoseAt(2.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetReplicated>(authority);

    // The client has not loaded the level yet.
    pair.Replicate();
    EXPECT_EQ(pair.LastApply.AuthoredDeferred, 1u);
    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 0u)
        << "an entity arrived before its level and was built anyway";
    EXPECT_FALSE(pair.Mirror(authority).IsValid());

    // The level loads.
    const EntityId local = Author(pair.Client, gate, PoseAt(2.0f, 0.0f, 0.0f));
    pair.Replicate();

    EXPECT_EQ(pair.LastApply.AuthoredBound, 1u);
    EXPECT_EQ(pair.Mirror(authority), local);
}

// A runtime spawn has no authored identity and must keep behaving exactly as
// it did: the wire creates it, because nothing else will.
TEST(ReplicationAuthored, ADynamicEntityIsStillCreatedByTheWire)
{
    Pair pair;
    pair.Authority.AddResource<PersistentEntityIndex>();
    pair.Client.AddResource<PersistentEntityIndex>();

    const EntityId authority = pair.SpawnReplicated(PoseAt(5.0f, 0.0f, 0.0f));
    pair.Replicate();

    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 1u);
    EXPECT_EQ(pair.LastApply.AuthoredBound, 0u);
    EXPECT_EQ(pair.LastApply.AuthoredDeferred, 0u);
    EXPECT_TRUE(pair.Mirror(authority).IsValid());
}

// A world with no index at all -- every test world before this, and every
// single-player one -- must not start deferring entities it would have built.
TEST(ReplicationAuthored, AWorldWithoutAnIndexIsUnaffected)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(6.0f, 0.0f, 0.0f));
    pair.Replicate();

    EXPECT_EQ(pair.LastApply.EntitiesSpawned, 1u);
    EXPECT_EQ(pair.LastApply.AuthoredDeferred, 0u);
    EXPECT_TRUE(pair.Mirror(authority).IsValid());
}

//=============================================================================
// Naming an entity across the wire
//
// Each side held one direction of the map, which is one direction short of
// what a message needs. An authority could not answer "which of my entities is
// this identity a peer just sent me", so it had no way to validate an object a
// peer names; a client could not answer "what do I call the thing I am looking
// at", so naming one meant walking every entry it had.
//=============================================================================

TEST(ReplicationIdentity, TheAuthorityResolvesAnIdentityItMinted)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Replicate();

    const NetEntityId id = pair.Identity.TryFind(authority);
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(pair.Identity.TryResolve(id), authority);
}

TEST(ReplicationIdentity, TheAuthorityNamesNothingForAnIdentityItNeverMinted)
{
    Pair pair;
    (void)pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Replicate();

    EXPECT_FALSE(pair.Identity.TryResolve(NetEntityId{ 9999 }).IsValid());
    EXPECT_FALSE(pair.Identity.TryResolve(NetEntityId{}).IsValid());
}

// The case a message has to survive: a peer naming something that has since
// died. Resolution stops as soon as the publish walk sweeps it.
TEST(ReplicationIdentity, AnIdentityStopsResolvingOnceItsEntityIsGone)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Replicate();
    const NetEntityId id = pair.Identity.TryFind(authority);
    ASSERT_TRUE(pair.Identity.TryResolve(id).IsValid());

    pair.Authority.DestroyEntity(authority);
    pair.Replicate();

    EXPECT_FALSE(pair.Identity.TryResolve(id).IsValid())
        << "an identity outlived the entity it named";
}

TEST(ReplicationIdentity, AClientNamesAnEntityReplicationGaveIt)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(2.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());

    const NetEntityId named = pair.ClientIdentity.TryFind(mirror);
    EXPECT_TRUE(named.IsValid());
    EXPECT_EQ(named, pair.Identity.TryFind(authority))
        << "the two sides disagree about what to call one entity";
}

// The security-relevant half. A client may only name what the authority gave
// it, so its own local entities have no wire name at all -- otherwise a peer
// could address something the authority has never heard of.
TEST(ReplicationIdentity, AClientCannotNameAnEntityItMadeItself)
{
    Pair pair;
    (void)pair.SpawnReplicated(PoseAt(3.0f, 0.0f, 0.0f));
    pair.Replicate();

    const EntityId local = pair.Client.CreateEntity();
    EXPECT_FALSE(pair.ClientIdentity.TryFind(local).IsValid());
}

TEST(ReplicationIdentity, AClientForgetsTheNameOfADestroyedEntity)
{
    Pair pair;
    const EntityId authority = pair.SpawnReplicated(PoseAt(4.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(pair.ClientIdentity.TryFind(mirror).IsValid());

    pair.Authority.DestroyEntity(authority);
    pair.Replicate();

    EXPECT_FALSE(pair.ClientIdentity.TryFind(mirror).IsValid())
        << "a name survived the entity it belonged to";
}

// Binding an identity onto a different entity -- which is what recognising an
// authored copy does -- must not leave the old pairing behind in either
// direction.
TEST(ReplicationIdentity, RebindingAnIdentityLeavesNoStaleName)
{
    ReplicationClientIdentity identity;
    const EntityId first{ 1, 1 };
    const EntityId second{ 2, 1 };

    identity.Bind(NetEntityId{ 7 }, first);
    identity.Bind(NetEntityId{ 7 }, second);

    EXPECT_EQ(identity.TryResolve(NetEntityId{ 7 }), second);
    EXPECT_EQ(identity.TryFind(second), NetEntityId{ 7 });
    EXPECT_FALSE(identity.TryFind(first).IsValid())
        << "the entity the identity moved off still answers to it";
}
