// Animation's steady-state wire cost: each case acknowledges its baseline, then
// measures the next snapshot above one describing nothing (the bare header), so
// the excess is the entity's envelope plus the records that changed.

#include "AnimRigFixture.h"

#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    struct AuthorityWire : AnimRigFixture
    {
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        ReplicationPeerState Peer;
        std::vector<std::byte> Scratch = std::vector<std::byte>(64 * 1024);
        std::uint64_t Generation = 0;
        DataAssetHandle Rig;
        EntityId Animated;
        std::vector<EntityId> Sources;

        AuthorityWire()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            components.Add<NetReplicated>();
            RegisterAnimationComponents(components);
            Schema.Seal();
            Layout.Seal();
            AnimCharacterRig::RegisterTags(*this);
            Rig = AnimCharacterRig::Load(*this);
            Animated = Character(Rig);
            Entities.AddComponent<NetReplicated>(Animated);
            for (int i = 0; i < 8; ++i)
                Sources.push_back(Entities.CreateEntity());
            Tick(2);
            (void)Publish();
        }

        std::size_t Publish()
        {
            Changes.Update(Entities, Layout, Identity, ++Generation);
            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.Tick = Generation;
            write.Sequence = Peer.NextSnapshotSequence();
            const SnapshotWriteResult result = ReplicationWriteSnapshot(write, Scratch);
            EXPECT_TRUE(result.Ok);
            NetSnapshotAck applied;
            applied.Observe(write.Sequence);
            Peer.Acknowledge(applied);
            return result.BytesWritten;
        }

        // Bytes the next snapshot carries above one that describes nothing.
        std::size_t Excess()
        {
            const std::size_t bytes = Publish();
            return bytes - StillSnapshot();
        }

        std::size_t StillSnapshot()
        {
            if (Still == 0)
            {
                AuthorityWire empty;
                empty.Tick(1);
                Still = empty.Publish();
            }
            return Still;
        }

        AnimRequestId IssueFrom(EntityId source)
        {
            AnimRequestDesc desc;
            desc.Source = source;
            desc.Intent = Tag("anim.intent.reload");
            const AnimRequestResult result = IssueAnimRequest(Entities, Animated, desc, Now);
            EXPECT_TRUE(result.Accepted());
            return result.Id;
        }

        std::size_t Still = 0;
    };
}

TEST(AnimRequestBandwidth, TheBaselineIsTheSnapshotHeader)
{
    AuthorityWire wire;
    constexpr std::size_t kHeaderBytes =
        (ReplicationSnapshotWire::TickBits + ReplicationSnapshotWire::SequenceBits
         + ReplicationSnapshotWire::CommandAckBits + 2 * ReplicationSnapshotWire::CountBits + 7) / 8;
    EXPECT_EQ(wire.StillSnapshot(), kHeaderBytes);
}

TEST(AnimRequestBandwidth, AnAnimatedEntityAtRestCostsNothing)
{
    AuthorityWire wire;
    wire.Tick(30);
    EXPECT_EQ(wire.Excess(), 0u) << "only the request set replicates; playing content is derived";
}

TEST(AnimRequestBandwidth, IssuingAndCancellingAHeldRequest)
{
    AuthorityWire wire;
    const AnimRequestId held = wire.IssueFrom(wire.Sources[0]);
    wire.Tick();
    EXPECT_LE(wire.Excess(), 90u) << "the entity's envelope and the new record";
    wire.Tick(10);
    EXPECT_EQ(wire.Excess(), 0u) << "a held request costs nothing while it holds";

    ASSERT_TRUE(CancelAnimRequest(wire.Entities, wire.Animated, held, AnimCancelReason::Released, wire.Now));
    wire.Tick();
    EXPECT_LE(wire.Excess(), 86u) << "the entity's envelope and the ended record";
}

TEST(AnimRequestBandwidth, AFullRequestSet)
{
    AuthorityWire wire;
    for (EntityId source : wire.Sources)
        (void)wire.IssueFrom(source);
    wire.Tick();
    EXPECT_LE(wire.Excess(), 650u) << "the entity's envelope and eight new records";
}
