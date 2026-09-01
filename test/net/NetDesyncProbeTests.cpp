#include <gtest/gtest.h>

#include <ecs/World.h>
#include <net/NetDesyncProbe.h>
#include <net/NetProtocol.h>
#include <movement/MovementComponentTraits.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationCodec.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <vector>

//=============================================================================
// Asking whether two machines agree, without sending the value
//
// The mechanism is a hash, so the tests that matter are not "the hash round
// trips". They are the two rules that keep it from reporting disagreement that
// is not disagreement -- only entities a peer has fully proved, and only fields
// that peer was eligible to receive -- because a check that fires on those gets
// turned off, and a check that is off catches nothing.
//=============================================================================

namespace
{
    struct ProbeTables
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;

        ProbeTables()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Layout.Seal();
        }
    };

    // An authority whose entities can be proved to a peer by hand, so the tests
    // can put a floor exactly where they mean to.
    struct ProbeFixture : ProbeTables
    {
        World Authority;
        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        ReplicationPeerState Peer;
        std::uint64_t Generation = 0;
        std::size_t Cursor = 0;
        std::vector<NetDesyncSample> Samples;

        World Client;
        ReplicationClientIdentity ClientIds;
        std::vector<std::byte> Scratch;

        ProbeFixture() : Scratch(64 * 1024)
        {
            Schema.Apply(Authority);
            Schema.Apply(Client);
        }

        // One snapshot, authority to client, so the two machines genuinely hold
        // the same values rather than being asserted to.
        void Replicate()
        {
            Publish();
            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.Sequence = Peer.NextSnapshotSequence();
            const SnapshotWriteResult written =
                ReplicationWriteSnapshot(write, Scratch);
            ASSERT_TRUE(written.Ok);

            SnapshotApplyRequest apply;
            apply.Target = &Client;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIds;
            const SnapshotApplyResult applied = ReplicationApplySnapshot(
                apply, std::span(Scratch).subspan(0, written.BytesWritten));
            ASSERT_TRUE(applied.Ok());

            NetSnapshotAck ack;
            ack.Observe(applied.Sequence);
            Peer.Acknowledge(ack);
        }

        [[nodiscard]] NetDesyncResult Check()
        {
            return NetCheckDesyncReport(Client, Layout, ClientIds, nullptr, 0,
                                        Samples);
        }

        EntityId Replicated(const Vec3d& at)
        {
            const EntityId entity = Authority.CreateEntity();
            Authority.AddComponent<NetReplicated>(entity, NetReplicated{});
            Transform3f placed;
            placed.Position = at;
            Authority.AddComponent<LocalTransform>(entity, LocalTransform{ placed });
            return entity;
        }

        void Publish() { Changes.Update(Authority, Layout, Identity, ++Generation); }

        // Marks everything the store currently holds as proved by this peer,
        // which is what an acknowledged snapshot does.
        void ProveAll()
        {
            const std::uint32_t sequence = Peer.NextSnapshotSequence();
            Peer.BeginSnapshot(sequence);
            for (const ReplicationChangeStore::EntityState& entity : Changes.Live())
                Peer.RecordSent(sequence, entity.Id, Generation);
            NetSnapshotAck ack;
            ack.Observe(sequence);
            Peer.Acknowledge(ack);
        }

        void Build(std::uint32_t ownerPeer = 0)
        {
            NetBuildDesyncReport(Changes, Layout, Peer, ownerPeer, Cursor, Samples);
        }

        [[nodiscard]] bool Sampled(EntityId entity) const
        {
            const NetEntityId id = Identity.TryFind(entity);
            for (const NetDesyncSample& sample : Samples)
            {
                if (sample.Id == id)
                    return true;
            }
            return false;
        }
    };
}

//-----------------------------------------------------------------------------
// The fold
//-----------------------------------------------------------------------------

TEST(NetDesyncFold, TheSameBytesFoldTheSame)
{
    ProbeTables tables;
    // Any component with at least one field; the fold does not care which.
    const ReplicatedComponent* transform = nullptr;
    for (const ReplicatedComponent& component : tables.Layout.Components())
    {
        if (!component.Fields.empty()
            && ReplicationVisibleFields(component, true) != 0)
        {
            transform = &component;
            break;
        }
    }
    ASSERT_NE(transform, nullptr) << "the engine table has nothing to fold";

    std::vector<std::byte> a(transform->Size, std::byte{ 0 });
    std::vector<std::byte> b = a;
    const std::uint64_t mask = ReplicationVisibleFields(*transform, true);

    EXPECT_EQ(ReplicationFoldFields(0, *transform, a, mask),
              ReplicationFoldFields(0, *transform, b, mask));

    // One byte inside a declared field changes the answer.
    b[transform->Fields[0].Offset] = std::byte{ 0x7f };
    EXPECT_NE(ReplicationFoldFields(0, *transform, a, mask),
              ReplicationFoldFields(0, *transform, b, mask));
}

// Only the bytes the fields declare. The padding between two fields is
// uninitialized on both machines, and folding it would report divergence in
// bytes neither of them has an opinion about.
TEST(NetDesyncFold, PaddingIsNotFolded)
{
    ProbeTables tables;
    for (const ReplicatedComponent& component : tables.Layout.Components())
    {
        // A component whose declared runs do not cover it entirely is the only
        // one that can show this; skip the ones that are wall to wall.
        std::size_t declared = 0;
        for (const ReplicatedField& field : component.Fields)
            declared += field.Size * field.Count;
        if (declared >= component.Size)
            continue;

        std::vector<std::byte> a(component.Size, std::byte{ 0 });
        std::vector<std::byte> b = a;
        const std::uint64_t mask = ReplicationVisibleFields(component, true);
        const std::uint64_t before = ReplicationFoldFields(0, component, a, mask);

        // Dirty every byte no field claims.
        std::vector<bool> claimed(component.Size, false);
        for (const ReplicatedField& field : component.Fields)
        {
            for (std::uint8_t i = 0; i < field.Count; ++i)
            {
                const std::size_t at = field.Offset + i * field.Size;
                for (std::size_t byte = 0; byte < field.Size; ++byte)
                    claimed[at + byte] = true;
            }
        }
        for (std::size_t byte = 0; byte < component.Size; ++byte)
        {
            if (!claimed[byte])
                b[byte] = std::byte{ 0xcd };
        }

        EXPECT_EQ(before, ReplicationFoldFields(0, component, b, mask))
            << component.Name << ": padding reached the hash";
        return;
    }
    GTEST_SKIP() << "no component in the engine table has padding to test";
}

// A field withheld from a peer must not be folded for that peer, or both sides
// are correct and the check still fires.
TEST(NetDesyncFold, AWithheldFieldDoesNotReachTheHash)
{
    ProbeTables tables;
    for (const ReplicatedComponent& component : tables.Layout.Components())
    {
        std::size_t gated = 0;
        for (const ReplicatedField& field : component.Fields)
        {
            if (field.OwnerOnly || field.OwnerLocal)
                ++gated;
        }
        if (gated == 0)
            continue;

        const std::uint64_t asOwner = ReplicationVisibleFields(component, true);
        const std::uint64_t asOther = ReplicationVisibleFields(component, false);
        ASSERT_NE(asOwner, asOther) << component.Name;

        std::vector<std::byte> bytes(component.Size, std::byte{ 0 });
        // Dirty an owner-only field: the owner's fold must move, a non-owner's
        // must not.
        for (std::size_t run = 0; run < component.Fields.size(); ++run)
        {
            if (!component.Fields[run].OwnerOnly)
                continue;
            const std::uint64_t ownerBefore =
                ReplicationFoldFields(0, component, bytes, asOwner);
            const std::uint64_t otherBefore =
                ReplicationFoldFields(0, component, bytes, asOther);
            bytes[component.Fields[run].Offset] = std::byte{ 0x5a };
            EXPECT_NE(ownerBefore,
                      ReplicationFoldFields(0, component, bytes, asOwner))
                << component.Name << ": the owner did not fold its own field";
            EXPECT_EQ(otherBefore,
                      ReplicationFoldFields(0, component, bytes, asOther))
                << component.Name
                << ": a field this peer was never sent reached its hash";
            return;
        }
    }
    GTEST_SKIP() << "no owner-gated field in the engine table";
}

//-----------------------------------------------------------------------------
// What the authority puts in a report
//-----------------------------------------------------------------------------

// A client's view is a mix of generations by design. Only an entity whose every
// run the peer already confirmed is one the two sides should agree about.
TEST(NetDesyncProbe, AnEntityThePeerHasNotProvedIsNotCompared)
{
    ProbeFixture fixture;
    const EntityId entity = fixture.Replicated(Vec3d{ 1.0f, 0.0f, 0.0f });
    fixture.Publish();

    fixture.Build();
    EXPECT_FALSE(fixture.Sampled(entity))
        << "an entity the peer has confirmed nothing about was compared";

    fixture.ProveAll();
    fixture.Build();
    EXPECT_TRUE(fixture.Sampled(entity));
}

// And one that has moved since it was proved is in flight, not divergent.
TEST(NetDesyncProbe, AnEntityThatMovedSinceItWasProvedIsNotCompared)
{
    ProbeFixture fixture;
    const EntityId entity = fixture.Replicated(Vec3d{ 1.0f, 0.0f, 0.0f });
    fixture.Publish();
    fixture.ProveAll();
    fixture.Build();
    ASSERT_TRUE(fixture.Sampled(entity));

    fixture.Authority.TryGet<LocalTransform>(entity)->Value.Position =
        Vec3d{ 9.0f, 0.0f, 0.0f };
    fixture.Publish();

    fixture.Cursor = 0;
    fixture.Build();
    EXPECT_FALSE(fixture.Sampled(entity))
        << "an entity with a change still in flight was reported as comparable";
}

TEST(NetDesyncProbe, AReportIsBounded)
{
    ProbeFixture fixture;
    for (std::size_t i = 0; i < kNetMaxDesyncSamples * 3; ++i)
        (void)fixture.Replicated(Vec3d{ static_cast<float>(i), 0.0f, 0.0f });
    fixture.Publish();
    fixture.ProveAll();

    fixture.Build();
    EXPECT_EQ(fixture.Samples.size(), kNetMaxDesyncSamples);
}

// Coverage comes from moving through the world, not from one large report.
TEST(NetDesyncProbe, SuccessiveReportsCoverDifferentEntities)
{
    ProbeFixture fixture;
    for (std::size_t i = 0; i < kNetMaxDesyncSamples * 2; ++i)
        (void)fixture.Replicated(Vec3d{ static_cast<float>(i), 0.0f, 0.0f });
    fixture.Publish();
    fixture.ProveAll();

    fixture.Build();
    const std::vector<NetDesyncSample> first = fixture.Samples;
    fixture.Build();

    ASSERT_EQ(first.size(), kNetMaxDesyncSamples);
    ASSERT_EQ(fixture.Samples.size(), kNetMaxDesyncSamples);
    EXPECT_NE(first.front().Id, fixture.Samples.front().Id)
        << "the probe reports the same entities forever";
}

//-----------------------------------------------------------------------------
// The wire
//-----------------------------------------------------------------------------

TEST(NetDesyncWire, AReportRoundTrips)
{
    const std::vector<NetDesyncSample> sent{
        { NetEntityId{ 7 }, 0x1122334455667788ull },
        { NetEntityId{ 9 }, 0xfeedfacecafebeefull },
    };
    std::vector<std::byte> bytes(256);
    const std::size_t size = NetEncodeDesyncReport(42, sent, bytes);
    ASSERT_GT(size, 0u);

    std::uint64_t tick = 0;
    std::vector<NetDesyncSample> got;
    ASSERT_TRUE(NetDecodeDesyncReport(
        std::span<const std::byte>(bytes).subspan(0, size), tick, got));
    EXPECT_EQ(tick, 42u);
    ASSERT_EQ(got.size(), sent.size());
    EXPECT_EQ(got[0].Id, sent[0].Id);
    EXPECT_EQ(got[0].Hash, sent[0].Hash);
    EXPECT_EQ(got[1].Id, sent[1].Id);
}

TEST(NetDesyncWire, TruncationAndTrailingBytesAreRefused)
{
    const std::vector<NetDesyncSample> sent{ { NetEntityId{ 3 }, 0xabcdull } };
    std::vector<std::byte> bytes(256);
    const std::size_t size = NetEncodeDesyncReport(1, sent, bytes);
    ASSERT_GT(size, 0u);

    std::uint64_t tick = 0;
    std::vector<NetDesyncSample> got;
    for (std::size_t keep = 0; keep < size; ++keep)
    {
        EXPECT_FALSE(NetDecodeDesyncReport(
            std::span<const std::byte>(bytes).subspan(0, keep), tick, got))
            << "accepted a report cut at " << keep;
    }
    EXPECT_FALSE(NetDecodeDesyncReport(
        std::span<const std::byte>(bytes).subspan(0, size + 1), tick, got));
}

TEST(NetDesyncWire, AnOversizedCountIsRefused)
{
    std::vector<std::byte> bytes(256, std::byte{ 0 });
    bytes[0] = static_cast<std::byte>(NetPayloadKind::DesyncHash);
    // tick occupies the next eight; the count byte follows.
    bytes[9] = static_cast<std::byte>(kNetMaxDesyncSamples + 1);

    std::uint64_t tick = 0;
    std::vector<NetDesyncSample> got;
    EXPECT_FALSE(NetDecodeDesyncReport(bytes, tick, got));
}

//-----------------------------------------------------------------------------
// The two machines, end to end
//-----------------------------------------------------------------------------

// The case that must be silent, and the one that decides whether anybody leaves
// this turned on.
TEST(NetDesyncProbe, TwoMachinesThatAgreeReportNothing)
{
    ProbeFixture fixture;
    (void)fixture.Replicated(Vec3d{ 1.0f, 2.0f, 3.0f });
    (void)fixture.Replicated(Vec3d{ -4.0f, 0.5f, 8.0f });
    fixture.Replicate();

    fixture.Build();
    ASSERT_FALSE(fixture.Samples.empty()) << "nothing was comparable";

    const NetDesyncResult result = fixture.Check();
    EXPECT_GT(result.Compared, 0u);
    EXPECT_EQ(result.Diverged, 0u);
    EXPECT_EQ(result.Absent, 0u);
}

// And the case it exists for: the client holds something the authority does
// not, and nothing else in the stack would ever say so.
TEST(NetDesyncProbe, AClientHoldingADifferentValueIsCaught)
{
    ProbeFixture fixture;
    const EntityId entity = fixture.Replicated(Vec3d{ 1.0f, 2.0f, 3.0f });
    fixture.Replicate();
    fixture.Build();
    ASSERT_FALSE(fixture.Samples.empty());
    ASSERT_EQ(fixture.Check().Diverged, 0u);

    // Standing in for the defect: a client whose copy drifted from what the
    // authority believes it was told.
    const NetEntityId id = fixture.Identity.TryFind(entity);
    const EntityId mirror = fixture.ClientIds.TryResolve(id);
    ASSERT_TRUE(mirror.IsValid());
    fixture.Client.TryGet<LocalTransform>(mirror)->Value.Position =
        Vec3d{ 99.0f, 2.0f, 3.0f };

    const NetDesyncResult result = fixture.Check();
    EXPECT_EQ(result.Diverged, 1u);
    EXPECT_EQ(result.FirstDiverged, id);
}

// An entity a snapshot has not delivered yet is absent, not divergent. The
// distinction is the difference between a diagnostic and a false alarm.
TEST(NetDesyncProbe, AnEntityTheClientHasNotReceivedIsAbsentNotDivergent)
{
    ProbeFixture fixture;
    (void)fixture.Replicated(Vec3d{ 1.0f, 0.0f, 0.0f });
    fixture.Replicate();
    fixture.Build();
    ASSERT_FALSE(fixture.Samples.empty());

    // A client that never received it at all.
    ProbeFixture bare;
    const NetDesyncResult result = NetCheckDesyncReport(
        bare.Client, bare.Layout, bare.ClientIds, nullptr, 0, fixture.Samples);

    EXPECT_EQ(result.Diverged, 0u);
    EXPECT_EQ(result.Compared, 0u);
    EXPECT_GT(result.Absent, 0u);
}

// The rule that decides whether this compares anything at all. The transform is
// a predicted component, so excluding predicted state everywhere -- rather than
// only on the entity a peer drives -- leaves the probe folding nothing and
// agreeing with everybody.
TEST(NetDesyncProbe, PredictedStateIsExcludedOnlyOnTheEntityAPeerDrives)
{
    ProbeFixture fixture;
    const EntityId entity = fixture.Replicated(Vec3d{ 3.0f, 4.0f, 5.0f });
    fixture.Replicate();

    // Nobody owns it: the transform is folded, so a difference is visible.
    fixture.Build(/*ownerPeer=*/0);
    ASSERT_EQ(fixture.Samples.size(), 1u);
    const std::uint64_t asStranger = fixture.Samples[0].Hash;

    // The peer driving it: its predicted state is skipped, because that machine
    // is deliberately ahead of the authority on it.
    fixture.Authority.AddComponent<NetOwner>(entity, NetOwner{ .Peer = 5 });
    fixture.Publish();
    fixture.Cursor = 0;
    // Re-prove, since ownership moving is itself a change.
    fixture.ProveAll();
    fixture.Cursor = 0;
    fixture.Build(/*ownerPeer=*/5);
    ASSERT_EQ(fixture.Samples.size(), 1u);

    EXPECT_NE(asStranger, fixture.Samples[0].Hash)
        << "the owner and a stranger folded the same fields, so either "
           "prediction is not being excluded or nothing is being folded";
}

// The other rule that keeps this from crying wolf. A field withheld from a peer
// was never sent to it, so that peer holds whatever its applier left there --
// and folding it on the authority but not on the client, or the reverse, reports
// divergence on every entity with an owner-gated field.
//
// SupportState is entirely owner-gated, which makes it the sharp case: a
// non-owner is meant to fold none of it.
TEST(NetDesyncProbe, OwnerGatedFieldsDoNotMakeANonOwnerLookDivergent)
{
    ProbeFixture fixture;
    const EntityId entity = fixture.Replicated(Vec3d{ 2.0f, 0.0f, 0.0f });
    SupportState support;
    support.Kind = SupportKind::Stable;
    support.SurfaceVelocity = Vec3d{ 7.0f, 0.0f, 0.0f };
    fixture.Authority.AddComponent<SupportState>(entity, support);
    // Driven by somebody who is not the peer this report is for.
    fixture.Authority.AddComponent<NetOwner>(entity, NetOwner{ .Peer = 5 });
    fixture.Replicate();

    // The report is built for peer 7, which owns nothing here.
    fixture.Build(/*ownerPeer=*/7);
    ASSERT_FALSE(fixture.Samples.empty());

    const NetDesyncResult result = NetCheckDesyncReport(
        fixture.Client, fixture.Layout, fixture.ClientIds, nullptr,
        /*selfPeer=*/7, fixture.Samples);

    EXPECT_GT(result.Compared, 0u);
    EXPECT_EQ(result.Diverged, 0u)
        << "a field this peer was never sent was folded into the comparison";
}
