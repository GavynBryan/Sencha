#include <ecs/PartitionFollowers.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeWorld.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace
{
    class HeldRecordTest : public ::testing::Test
    {
    protected:
        HeldRecordTest()
        {
            ComponentRegistrar registrar(&Schema, nullptr, nullptr);
            registrar.Add<PersistentIdComponent>();
            registrar.Add<Held>();
            registrar.Add<NetReplicated>();
            registrar.Add<NetRelevanceAnchor>();
            Schema.Seal();
            Schema.Apply(Entities);
            Entities.AddResource<PersistentEntityIndex>();
            Index = &InstallHeldRecords(Entities);
        }

        EntityId Holder(StoragePartitionId partition = StoragePartitionId::Default())
        {
            return Entities.CreateEntity(partition);
        }

        EntityId HeldBy(EntityId holder, std::optional<std::uint16_t> position = {})
        {
            const EntityId record = Entities.CreateEntity();
            HoldRecord(Entities, record, holder, position);
            return record;
        }

        std::vector<EntityId> RecordsOf(EntityId holder) const
        {
            const std::span<const EntityId> records = Index->RecordsOf(holder);
            return { records.begin(), records.end() };
        }

        WorldComponentSchema Schema;
        World Entities;
        HeldRecordIndex* Index = nullptr;
    };
}

TEST_F(HeldRecordTest, RecordsAreListedByPositionThenByWhenTheyWereHeld)
{
    const EntityId chest = Holder();
    const EntityId second = HeldBy(chest, 2);
    const EntityId first = HeldBy(chest, 0);
    const EntityId alsoSecond = HeldBy(chest, 2);
    const EntityId last = HeldBy(chest);
    const EntityId worlds = HeldBy(EntityId{});

    EXPECT_EQ(RecordsOf(chest), (std::vector{ first, second, alsoSecond, last }));
    EXPECT_EQ(Entities.TryGet<Held>(last)->Position, 3u);
    EXPECT_EQ(RecordsOf(EntityId{}), std::vector{ worlds });
    EXPECT_EQ(Index->HolderOf(second), chest);
    EXPECT_FALSE(Index->HolderOf(worlds).IsValid());
}

TEST_F(HeldRecordTest, PositionsPastTheLastTieInTheOrderTheyWereHeldAndAreCounted)
{
    const EntityId chest = Holder();
    const EntityId atLast = HeldBy(chest, 0xFFFF);
    const EntityId past = HeldBy(chest);
    const EntityId further = HeldBy(chest);

    EXPECT_EQ(Entities.TryGet<Held>(past)->Position, 0xFFFFu);
    EXPECT_EQ(RecordsOf(chest), (std::vector{ atLast, past, further }));
    EXPECT_EQ(Index->PositionsExhausted(), 2u);
}

TEST_F(HeldRecordTest, HoldingARecordGivesItIdentityReplicationAndItsHoldersRelevance)
{
    const EntityId body = Holder(StoragePartitionId{ 3 });
    const EntityId record = HeldBy(body);
    const EntityId worlds = HeldBy(EntityId{});

    ASSERT_NE(Entities.TryGet<PersistentIdComponent>(record), nullptr);
    ASSERT_NE(Entities.TryGet<PersistentIdComponent>(body), nullptr);
    EXPECT_EQ(Entities.TryGet<Held>(record)->Holder.Target, Entities.TryGet<PersistentIdComponent>(body)->Id)
        << "a holder is named by its persistent id, so a save can name it";
    EXPECT_TRUE(Entities.HasComponent<NetReplicated>(record));
    ASSERT_NE(Entities.TryGet<NetRelevanceAnchor>(record), nullptr);
    EXPECT_EQ(Entities.TryGet<NetRelevanceAnchor>(record)->Anchor, body);
    EXPECT_EQ(Entities.GetEntityPartition(record), StoragePartitionId{ 3 });
    EXPECT_EQ(Entities.TryGet<NetRelevanceAnchor>(worlds), nullptr) << "the world's records reach every peer";
    EXPECT_EQ(Entities.GetEntityPartition(worlds), PersistentStoragePartition);
}

TEST_F(HeldRecordTest, ChangingTheHolderMovesTheRecordAndBothRevisions)
{
    const EntityId first = Holder();
    const EntityId second = Holder();
    const EntityId record = HeldBy(first);
    const std::uint32_t firstBefore = Index->Revision(first);
    const std::uint32_t secondBefore = Index->Revision(second);

    HoldRecord(Entities, record, second);

    EXPECT_TRUE(RecordsOf(first).empty());
    EXPECT_EQ(RecordsOf(second), std::vector{ record });
    EXPECT_GT(Index->Revision(first), firstBefore);
    EXPECT_GT(Index->Revision(second), secondBefore);
    EXPECT_EQ(Entities.TryGet<NetRelevanceAnchor>(record)->Anchor, second);

    Entities.DestroyEntity(record);
    EXPECT_TRUE(RecordsOf(second).empty());
    EXPECT_EQ(Index->Count(), 0u);
}

TEST_F(HeldRecordTest, AChangeBeneathANestedHolderMovesEveryRevisionAboveIt)
{
    const EntityId body = Holder();
    const EntityId bag = HeldBy(body);
    const EntityId coin = HeldBy(bag);
    const std::uint32_t bodyBefore = Index->Revision(body);
    const std::uint32_t bagBefore = Index->Revision(bag);

    Index->NoteChanged(coin);

    EXPECT_GT(Index->Revision(bag), bagBefore);
    EXPECT_GT(Index->Revision(body), bodyBefore);
}

TEST_F(HeldRecordTest, AHolderGoingAwayTakesWhatItHeldWhenRecordsSettle)
{
    const EntityId body = Holder();
    const EntityId bag = HeldBy(body);
    const EntityId coin = HeldBy(bag);
    const EntityId worlds = HeldBy(EntityId{});

    Entities.DestroyEntity(body);
    EXPECT_TRUE(Entities.IsAlive(bag)) << "a hook cannot destroy; the records wait for the settle";

    SettleHeldRecords(Entities);

    EXPECT_FALSE(Entities.IsAlive(bag));
    EXPECT_FALSE(Entities.IsAlive(coin)) << "what the held record held goes with it";
    EXPECT_TRUE(Entities.IsAlive(worlds));
    EXPECT_EQ(Index->Count(), 1u);
}

TEST_F(HeldRecordTest, ANewIdentityOnALiveHolderTakesNothing)
{
    const EntityId body = Holder();
    const EntityId record = HeldBy(body);
    const PersistentEntityId before = Entities.TryGet<PersistentIdComponent>(body)->Id;

    (void)Entities.ReplaceComponent<PersistentIdComponent>(
        body, PersistentIdComponent{ PersistentEntityId{ before.Value + 1000 } });
    SettleHeldRecords(Entities);

    EXPECT_TRUE(Entities.IsAlive(record));
    EXPECT_EQ(RecordsOf(body), std::vector{ record });
}

TEST_F(HeldRecordTest, RecordsFollowTheirHolderAcrossPartitions)
{
    const EntityId chest = Holder(StoragePartitionId{ 1 });
    const EntityId bag = HeldBy(chest);
    const EntityId coin = HeldBy(bag);
    (void)DispatchPartitionMoves(Entities);

    (void)Entities.MoveEntityToPartition(chest, StoragePartitionId{ 2 });
    (void)DispatchPartitionMoves(Entities);

    EXPECT_EQ(Entities.GetEntityPartition(bag), StoragePartitionId{ 2 });
    EXPECT_EQ(Entities.GetEntityPartition(coin), StoragePartitionId{ 2 });
}

TEST_F(HeldRecordTest, ARecordWhoseHolderIsNotResidentWaitsForItsIdentity)
{
    constexpr PersistentEntityId kBody{ 0x4242 };
    const EntityId record = Entities.CreateEntity();
    Entities.AddComponent<Held>(record, Held{ .Holder = { .Entity = {}, .Target = kBody }, .Position = 0 });
    SettleHeldRecords(Entities);

    EXPECT_EQ(Index->AwaitedHolderOf(record), kBody);
    EXPECT_TRUE(RecordsOf(EntityId{}).empty()) << "waiting is not the world";
    ASSERT_NE(Entities.TryGet<NetRelevanceAnchor>(record), nullptr);
    EXPECT_FALSE(Entities.TryGet<NetRelevanceAnchor>(record)->Anchor.IsValid()) << "it reaches nobody while it waits";

    const EntityId body = Holder();
    Entities.AddComponent<PersistentIdComponent>(body, PersistentIdComponent{ kBody });
    SettleHeldRecords(Entities);

    EXPECT_EQ(RecordsOf(body), std::vector{ record });
    EXPECT_EQ(Entities.TryGet<NetRelevanceAnchor>(record)->Anchor, body);
}

TEST_F(HeldRecordTest, AnOverwriteFromTheWireRunsTheHooks)
{
    const EntityId first = Holder();
    const EntityId second = Holder();
    const EntityId record = HeldBy(first);
    Held moved = *Entities.TryGet<Held>(record);
    moved.Holder = PersistableRef{ .Entity = second, .Target = {} };
    std::byte bytes[sizeof(Held)];
    std::memcpy(bytes, &moved, sizeof(Held));

    ASSERT_TRUE(Schema.SetComponentBytes(Entities, record, ResolveComponentTypeId<Held>(), bytes));

    EXPECT_TRUE(RecordsOf(first).empty());
    EXPECT_EQ(RecordsOf(second), std::vector{ record });
}

TEST_F(HeldRecordTest, DestroyingEveryRecordLeavesTheIndexEmpty)
{
    const EntityId body = Holder();
    (void)HeldBy(HeldBy(body));
    (void)HeldBy(EntityId{});
    const EntityId waiting = Entities.CreateEntity();
    Entities.AddComponent<Held>(waiting, Held{ .Holder = { .Entity = {}, .Target = PersistentEntityId{ 9 } } });

    DestroyHeldRecords(Entities);

    EXPECT_EQ(Index->Count(), 0u);
    EXPECT_FALSE(Entities.IsAlive(waiting));
    EXPECT_TRUE(Entities.IsAlive(body));
}
