#include <ecs/WorldComponentSchema.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventorySystem.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
    constexpr std::string_view kArrow = "asset://items/arrow.sdata";

    struct Peer
    {
        explicit Peer(std::uint32_t id)
            : Id(id)
        {
        }

        std::uint32_t Id;
        World Entities;
        ReplicationPeerState State;
        ReplicationClientIdentity Identity;
        NetSnapshotAck Ack;
    };

    struct Session
    {
        Session()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Layout.Seal();
            Schema.Apply(Authority);
            Authority.AddResource<PersistentEntityIndex>();
            (void)InstallHeldRecords(Authority);
            InstallInventoryVocabulary(Authority);
            Arrows = Authority.GetResource<ItemRegistry>().Define(std::string(kArrow),
                                                                  ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
        }

        Peer& Join(std::uint32_t id)
        {
            Peers.push_back(std::make_unique<Peer>(id));
            Peer& peer = *Peers.back();
            Schema.Apply(peer.Entities);
            peer.Entities.AdvanceFrame();
            peer.Entities.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
            peer.Entities.AddResource<PersistentEntityIndex>();
            (void)InstallHeldRecords(peer.Entities);
            InstallInventoryVocabulary(peer.Entities);
            (void)peer.Entities.GetResource<ItemRegistry>().Reserve("asset://items/first_here.sdata");
            (void)peer.Entities.GetResource<ItemRegistry>().Reserve(kArrow);
            return peer;
        }

        EntityId Holder(std::uint32_t owner)
        {
            const EntityId holder = Authority.CreateEntity();
            Authority.AddComponent<NetReplicated>(holder, NetReplicated{});
            Authority.AddComponent<LocalTransform>(holder, LocalTransform{});
            Authority.AddComponent<ItemContainer>(holder, ItemContainer{});
            if (owner != kNetAuthorityPeer)
                Authority.AddComponent<NetOwner>(holder, NetOwner{ .Peer = owner });
            return holder;
        }

        void Publish()
        {
            RunInventoryPass(Authority);
            ++Tick;
            Changes.Update(Authority, Layout, Identity, ++Generation);
            for (const std::unique_ptr<Peer>& peer : Peers)
            {
                SnapshotWriteRequest write;
                write.Changes = &Changes;
                write.Layout = &Layout;
                write.Peer = &peer->State;
                write.OwnerPeer = peer->Id;
                write.Tick = Tick;
                write.Sequence = peer->State.NextSnapshotSequence();
                const SnapshotWriteResult written = ReplicationWriteSnapshot(write, Scratch);
                ASSERT_TRUE(written.Ok);
                peer->Entities.AdvanceFrame();
                SnapshotApplyRequest apply;
                apply.Target = &peer->Entities;
                apply.Schema = &Schema;
                apply.Layout = &Layout;
                apply.Identity = &peer->Identity;
                const SnapshotApplyResult applied =
                    ReplicationApplySnapshot(apply, std::span(Scratch).subspan(0, written.BytesWritten));
                ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
                peer->Ack.Observe(applied.Sequence);
                peer->State.Acknowledge(peer->Ack);
            }
        }

        [[nodiscard]] EntityId MirrorOf(const Peer& peer, EntityId entity) const
        {
            const NetEntityId id = Identity.TryFind(entity);
            return id.IsValid() ? peer.Identity.TryResolve(id) : EntityId{};
        }

        [[nodiscard]] std::vector<std::uint32_t> CountsHeld(Peer& peer, EntityId holder) const
        {
            std::vector<std::uint32_t> counts;
            for (const EntityId entry : ItemsIn(peer.Entities, MirrorOf(peer, holder)))
                counts.push_back(peer.Entities.TryGet<ItemEntry>(entry)->Count);
            return counts;
        }

        [[nodiscard]] static std::size_t EntriesOn(const World& world)
        {
            std::size_t entries = 0;
            for (const EntityId entity : world.GetAliveEntities())
                entries += world.HasComponent<ItemEntry>(entity) ? 1 : 0;
            return entries;
        }

        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Authority;
        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        std::vector<std::unique_ptr<Peer>> Peers;
        std::vector<std::byte> Scratch = std::vector<std::byte>(64 * 1024);
        std::uint64_t Generation = 0;
        std::uint64_t Tick = 0;
        ItemId Arrows;
    };
}

TEST(InventoryReplication, EachPeerHoldsItsOwnAndALateJoinerIsSeededWithItsOwnOnly)
{
    Session session;
    Peer& first = session.Join(1);
    const EntityId archer = session.Holder(1);
    const EntityId hunter = session.Holder(2);
    const EntityId chest = session.Holder(kNetAuthorityPeer);
    Inventory inventory(session.Authority, FixedSimTime{});
    ASSERT_EQ(inventory.Give(archer, session.Arrows, 30).Refusal, InventoryRefusal::None);
    ASSERT_EQ(inventory.Give(hunter, session.Arrows, 5).Refusal, InventoryRefusal::None);
    ASSERT_EQ(inventory.Give(chest, session.Arrows, 9).Refusal, InventoryRefusal::None);
    session.Publish();

    EXPECT_EQ(session.CountsHeld(first, archer), (std::vector<std::uint32_t>{ 20, 10 }));
    EXPECT_EQ(Session::EntriesOn(first.Entities), 2u) << "the hunter's quiver and the chest reached the archer";
    EXPECT_TRUE(session.MirrorOf(first, hunter).IsValid()) << "the hunter is still there to see";

    const EntityId stowed = ItemsIn(session.Authority, archer)[1];
    ASSERT_EQ(inventory.Transfer(stowed, chest, 0).Refusal, InventoryRefusal::None);
    session.Publish();
    EXPECT_EQ(session.CountsHeld(first, archer), (std::vector<std::uint32_t>{ 20 }));
    EXPECT_EQ(Session::EntriesOn(first.Entities), 1u);

    Peer& late = session.Join(2);
    session.Publish();
    EXPECT_EQ(session.CountsHeld(late, hunter), (std::vector<std::uint32_t>{ 5 }));
    EXPECT_EQ(Session::EntriesOn(late.Entities), 1u);
    EXPECT_TRUE(session.MirrorOf(late, archer).IsValid());
    EXPECT_TRUE(session.CountsHeld(late, archer).empty());

    session.Authority.TryGet<ItemEntry>(ItemsIn(session.Authority, hunter)[0])->Count = 3;
    session.Publish();
    EXPECT_EQ(session.CountsHeld(late, hunter), (std::vector<std::uint32_t>{ 3 }));
    EXPECT_EQ(session.CountsHeld(first, archer), (std::vector<std::uint32_t>{ 20 }));
}
