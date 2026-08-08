#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <net/ClientPrediction.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <ecs/Query.h>
#include <net/NetSpawnRecipe.h>
#include <net/ReplicationInterpolation.h>
#include <net/ReplicationSnapshot.h>
#include <world/transform/TransformHistory.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{
    constexpr std::size_t kSnapshotBytes = 64 * 1024;

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
        ReplicationPeerState Peer;
        ReplicationClientIdentity ClientIdentity;

        std::vector<std::byte> Scratch;
        std::uint64_t Tick = 0;
        SnapshotWriteResult LastWrite;
        SnapshotApplyResult LastApply;

        Pair() : Scratch(kSnapshotBytes)
        {
            RegisterEngineRuntimeComponents(Schema);
            Schema.Seal();
            Schema.Apply(Authority);
            Schema.Apply(Client);

            RegisterEngineReplicatedComponents(Layout);
            Layout.Seal();
        }

        // One snapshot: written on the authority, carried as bytes, applied on
        // the client. Returns the bytes it took.
        std::size_t Replicate(std::uint32_t ownerPeer = 0,
                              const NetSpawnRecipes* recipes = nullptr,
                              ClientPrediction* prediction = nullptr,
                              ReplicationInterpolation* interpolation = nullptr,
                              std::uint64_t commandAck = 0)
        {
            ++Tick;
            SnapshotWriteRequest write;
            write.Source = &Authority;
            write.Layout = &Layout;
            write.Identity = &Identity;
            write.Peer = &Peer;
            write.OwnerPeer = ownerPeer;
            write.Tick = Tick;
            write.CommandAck = commandAck;

            LastWrite = ReplicationWriteSnapshot(write, Scratch);
            EXPECT_TRUE(LastWrite.Ok);

            SnapshotApplyRequest apply;
            apply.Target = &Client;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIdentity;
            apply.Recipes = recipes;
            apply.Prediction = prediction;
            apply.Interpolation = interpolation;

            LastApply = ReplicationApplySnapshot(
                apply, std::span(Scratch).subspan(0, LastWrite.BytesWritten));
            EXPECT_TRUE(LastApply.Ok())
                << SnapshotApplyErrorToString(LastApply.Error);
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
    EXPECT_FLOAT_EQ(transform->Value.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(transform->Value.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(transform->Value.Position.Z, 3.0f);
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
        ASSERT_FLOAT_EQ(seen->Value.Position.X, static_cast<float>(step))
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
        EXPECT_FLOAT_EQ(seen->Value.Position.X, static_cast<float>(i));
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
        EXPECT_FLOAT_EQ(seen->Value.Position.X, static_cast<float>(i));
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
TEST(ReplicationSnapshot, AStillWorldCostsAlmostNothingAfterTheFirstSnapshot)
{
    Pair pair;
    for (int i = 0; i < 8; ++i)
        pair.SpawnReplicated(PoseAt(static_cast<float>(i), 0.0f, 0.0f));

    const std::size_t first = pair.Replicate();
    const std::size_t second = pair.Replicate();
    const std::size_t third = pair.Replicate();

    EXPECT_LT(second, first)
        << "the second snapshot of an unchanged world must be smaller than the first";
    EXPECT_EQ(second, third) << "and steady from then on";
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
    EXPECT_FLOAT_EQ(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X, 99.0f);

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

    EXPECT_FLOAT_EQ(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    9.0f)
        << "the authority's position was written over what this machine "
           "predicted, which is the round trip the player would feel";
}

// The trap the authoritative shadow exists for.
//
// A snapshot carries fields, not components: the authority moves along X every
// tick, so X is sent and Z -- which it never touches -- is not. This machine
// meanwhile drifts along Z. Staged against what this machine simulated, the
// unsent Z would be filled in from its own drift and the divergence would read
// as perfect agreement; staged against the authority's own view, it is seen.
TEST(ReplicationPrediction, AnUnsentFieldIsComparedAgainstTheAuthorityNotThePrediction)
{
    Pair pair;
    ClientPrediction prediction;

    const EntityId authority = pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));
    pair.Replicate();
    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    prediction.SetPredicted(mirror);

    LocalTransform* authorityPose = pair.Authority.TryGet<LocalTransform>(authority);
    LocalTransform* clientPose = pair.Client.TryGet<LocalTransform>(mirror);

    // A tick where the two agree, which seeds the authority's view.
    authorityPose->Value.Position = Vec3d{ 1.0f, 0.0f, 0.0f };
    clientPose->Value.Position = Vec3d{ 1.0f, 0.0f, 0.0f };
    prediction.Record(pair.Tick + 1, clientPose->Value.Position);
    pair.Replicate(0, nullptr, &prediction);
    ASSERT_TRUE(prediction.HasAuthoritativeState(ResolveComponentTypeId<LocalTransform>()));
    ASSERT_FALSE(pair.LastApply.Prediction.has_value());

    // The authority advances along X only. This machine advances along X too --
    // agreeing there -- and has also drifted three quarters of a metre along Z,
    // which the authority has never had reason to send.
    authorityPose->Value.Position = Vec3d{ 2.0f, 0.0f, 0.0f };
    clientPose->Value.Position = Vec3d{ 2.0f, 0.0f, 0.75f };
    prediction.Record(pair.Tick + 1, clientPose->Value.Position);
    pair.Replicate(0, nullptr, &prediction);

    ASSERT_TRUE(pair.LastApply.Prediction.has_value())
        << "a divergence in a field the authority had no reason to resend was "
           "compared against this machine's own guess and read as agreement";
    EXPECT_FLOAT_EQ(pair.LastApply.Prediction->Offset.Z, -0.75f);
    EXPECT_NEAR(pair.LastApply.Prediction->Offset.X, 0.0f, 1e-4f)
        << "the axis both machines agree on is not a correction";
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
        KinematicState motion;
        motion.Velocity = Vec3d{ 3.0f, -1.0f, 0.5f };
        pair.Authority.AddComponent<KinematicState>(pawn, motion);
        SupportState support;
        support.Kind = SupportKind::Stable;
        support.SurfaceVelocity = Vec3d{ 0.25f, 0.0f, 0.0f };
        pair.Authority.AddComponent<SupportState>(pawn, support);
        pair.Authority.AddComponent<CharacterMovement>(
            pawn, CharacterMovement{ .Mode = LocomotionModeId{ 2 } });
        pair.Authority.AddComponent<JumpState>(
            pawn, JumpState{ .CooldownRemaining = 0.12f });
        return pawn;
    }
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
    EXPECT_FLOAT_EQ(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    4.0f);

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

    const EntityId pawn = SpawnOwnedPawn(pair, 7);
    pair.Replicate(7, nullptr, &prediction);
    const EntityId mirror = pair.Mirror(pawn);
    ASSERT_TRUE(mirror.IsValid());
    prediction.SetPredicted(mirror);

    // First snapshot with a subject: everything lands in the shadows.
    pair.Replicate(7, nullptr, &prediction);
    ASSERT_TRUE(prediction.HasAuthoritativeState(
        ResolveComponentTypeId<KinematicState>()));

    // The authority changes nothing, so the next delta carries empty masks.
    pair.Replicate(7, nullptr, &prediction);

    World scratch;
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    schema.Apply(scratch);
    const EntityId probe = scratch.CreateEntity();
    scratch.AddComponent<KinematicState>(probe, KinematicState{});
    scratch.AddComponent<SupportState>(probe, SupportState{});
    scratch.AddComponent<CharacterMovement>(probe, CharacterMovement{});
    scratch.AddComponent<JumpState>(probe, JumpState{});
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

    EXPECT_FLOAT_EQ(
        pair.Client.TryGet<LocalTransform>(theirMirror)->Value.Position.X, 42.0f)
        << "a puppet has to be drawn where the authority says it is";
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
    EXPECT_FLOAT_EQ(held->Value.Position.X, 9.0f);

    // And the world still holds whatever it did: writing the arriving pose here
    // is exactly the stepping the buffer exists to replace.
    EXPECT_FLOAT_EQ(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    0.0f)
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
    EXPECT_FLOAT_EQ(held->Value.Position.X, 6.0f);
    EXPECT_FLOAT_EQ(held->Value.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(held->Value.Position.Z, 4.0f);
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

    EXPECT_FLOAT_EQ(pair.Client.TryGet<LocalTransform>(mirror)->Value.Position.X,
                    3.0f)
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
        EXPECT_FLOAT_EQ(held->Value.Position.X, 0.0f)
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
    EXPECT_FLOAT_EQ(derived->Value.Position.X, 3.0f);
    EXPECT_FLOAT_EQ(derived->Value.Position.Y, 4.0f);
    EXPECT_FLOAT_EQ(derived->Value.Position.Z, 5.0f);
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
        ASSERT_FLOAT_EQ(derived->Value.Position.X, static_cast<float>(step) * 2.0f)
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

TEST(ReplicationVisibility, ARecipeCompletesTheEntityOnArrival)
{
    Pair pair;
    NetSpawnRecipes recipes;
    int built = 0;
    // Written the way a real recipe is: idempotent, so that "ran once" is
    // proven by the counter rather than by a duplicate add bringing the process
    // down. A test that detects a regression by crashing reports it as no
    // output at all, which is easy to misread as passing.
    recipes.Register(7, [&built](World& world, EntityId entity) {
        ++built;
        if (!world.HasComponent<WorldTransformHistory>(entity))
            world.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{});
    });

    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetSpawnRecipe>(authority, NetSpawnRecipe{ .Id = 7 });
    pair.Replicate(0, &recipes);

    const EntityId mirror = pair.Mirror(authority);
    ASSERT_TRUE(mirror.IsValid());
    EXPECT_EQ(built, 1);
    EXPECT_TRUE(pair.Client.HasComponent<WorldTransformHistory>(mirror))
        << "the recipe did not run, so the entity arrived bare";

    // And only once, however many snapshots follow.
    pair.Replicate(0, &recipes);
    pair.Replicate(0, &recipes);
    EXPECT_EQ(built, 1) << "a recipe must build an entity once, not every tick";
}

// A recipe this build does not know leaves the entity with its state rather
// than failing the snapshot: an authority may run content a client lacks.
TEST(ReplicationVisibility, AnUnknownRecipeIsCountedNotFatal)
{
    Pair pair;
    NetSpawnRecipes recipes;

    const EntityId authority = pair.SpawnReplicated(PoseAt(1.0f, 0.0f, 0.0f));
    pair.Authority.AddComponent<NetSpawnRecipe>(authority, NetSpawnRecipe{ .Id = 99 });
    pair.Replicate(0, &recipes);

    EXPECT_TRUE(pair.LastApply.Ok());
    EXPECT_EQ(pair.LastApply.RecipesMissing, 1u);
    EXPECT_TRUE(pair.Mirror(authority).IsValid())
        << "the entity still exists with the state it was sent";
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
    write.Source = &pair.Authority;
    write.Layout = &pair.Layout;
    write.Identity = &pair.Identity;
    write.Peer = &pair.Peer;
    write.Tick = 1;
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
    pair.SpawnReplicated(PoseAt(0.0f, 0.0f, 0.0f));

    SnapshotWriteRequest write;
    write.Source = &pair.Authority;
    write.Layout = &pair.Layout;
    write.Identity = &pair.Identity;
    write.Peer = &pair.Peer;
    write.Tick = 1;
    const SnapshotWriteResult produced =
        ReplicationWriteSnapshot(write, pair.Scratch);
    ASSERT_TRUE(produced.Ok);

    // Where the first component key sits, counted rather than guessed so that
    // adding a header field moves one number here instead of a magic one.
    constexpr std::size_t kHeaderBits = 64   // tick
                                      + 64   // command acknowledgement
                                      + 32   // destroyed count
                                      + 32;  // updated count
    constexpr std::size_t kComponentKeyByte = (kHeaderBits + 64 + 8) / 8;
    ASSERT_GT(produced.BytesWritten, kComponentKeyByte);
    pair.Scratch[kComponentKeyByte] = std::byte{ 0xFE };

    SnapshotApplyRequest apply;
    apply.Target = &pair.Client;
    apply.Schema = &pair.Schema;
    apply.Layout = &pair.Layout;
    apply.Identity = &pair.ClientIdentity;

    const SnapshotApplyResult result = ReplicationApplySnapshot(
        apply, std::span(pair.Scratch).subspan(0, produced.BytesWritten));
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
    writer.WriteU64(0);            // command acknowledgement
    writer.WriteBits(0, 32);       // destroyed
    writer.WriteBits(0xFFFFFFFF, 32);  // updated: four billion entities

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
