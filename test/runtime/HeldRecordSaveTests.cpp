#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <save/HeldRecordSaves.h>
#include <world/ComponentRegistrar.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    struct SavingWorld
    {
        SavingWorld()
            : Context(Logging)
        {
            ComponentRegistrar registrar(&Schema, &Serializers, nullptr);
            registrar.Add<PersistentIdComponent>();
            registrar.Add<Held>();
            Schema.Seal();
            Schema.Apply(Entities);
            Entities.AddResource<PersistentEntityIndex>();
            Index = &InstallHeldRecords(Entities);
        }

        EntityId Identified(PersistentEntityId id)
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ id });
            return entity;
        }

        EntityId HeldBy(EntityId holder)
        {
            const EntityId record = Entities.CreateEntity();
            HoldRecord(Entities, record, holder);
            return record;
        }

        PersistentEntityId IdOf(EntityId entity) const { return Entities.TryGet<PersistentIdComponent>(entity)->Id; }

        EntityId Resolve(PersistentEntityId id) const
        {
            return Entities.TryGetResource<PersistentEntityIndex>()->TryResolve(id);
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        WorldComponentSchema Schema;
        ComponentSerializerRegistry Serializers;
        World Entities;
        HeldRecordIndex* Index = nullptr;
    };
}

TEST(HeldRecordSaves, WhatTheWorldAndARootHoldIsSavedButNotWhatOthersHold)
{
    SavingWorld world;
    const EntityId body = world.Identified(PersistentEntityId{ 0x10 });
    const EntityId stranger = world.Identified(PersistentEntityId{ 0x20 });
    (void)world.HeldBy(EntityId{});
    const EntityId bag = world.HeldBy(body);
    (void)world.HeldBy(bag);
    (void)world.HeldBy(stranger);

    const JsonValue saved = CaptureHeldRecords(world.Entities, std::span(&body, 1), world.Serializers, world.Context);

    ASSERT_NE(saved.Find("records"), nullptr);
    EXPECT_EQ(saved.Find("records")->AsArray().size(), 3u) << "the world's, the body's and what the body's bag holds";
}

TEST(HeldRecordSaves, ARestoreLeavesExactlyTheSavedRecordsUnderTheirIdsAndHolders)
{
    SavingWorld world;
    const EntityId body = world.Identified(PersistentEntityId{ 0x10 });
    const EntityId worlds = world.HeldBy(EntityId{});
    const EntityId bag = world.HeldBy(body);
    const EntityId coin = world.HeldBy(bag);
    const PersistentEntityId worldsId = world.IdOf(worlds);
    const PersistentEntityId bagId = world.IdOf(bag);
    const PersistentEntityId coinId = world.IdOf(coin);
    const JsonValue saved = CaptureHeldRecords(world.Entities, std::span(&body, 1), world.Serializers, world.Context);

    world.Entities.DestroyEntity(coin);
    (void)world.HeldBy(EntityId{});
    (void)world.HeldBy(body);
    RestoreHeldRecords(world.Entities, saved, world.Serializers, world.Context);
    SettleHeldRecords(world.Entities);

    EXPECT_EQ(world.Index->Count(), 3u);
    EXPECT_EQ(world.Index->HolderOf(world.Resolve(worldsId)), EntityId{});
    EXPECT_EQ(world.Index->HolderOf(world.Resolve(bagId)), body);
    EXPECT_EQ(world.Index->HolderOf(world.Resolve(coinId)), world.Resolve(bagId));
    EXPECT_EQ(world.Entities.GetResource<PersistentEntityIndex>().CollisionCount(), 0u)
        << "the old session's records were gone before the saved ones came back";
}

TEST(HeldRecordSaves, AFreshWorldTakesTheSavedRecordsAndTheBodysWaitForIt)
{
    JsonValue saved;
    PersistentEntityId bagId;
    {
        SavingWorld before;
        const EntityId body = before.Identified(PersistentEntityId{ 0x10 });
        (void)before.HeldBy(EntityId{});
        const EntityId bag = before.HeldBy(body);
        bagId = before.IdOf(bag);
        saved = CaptureHeldRecords(before.Entities, std::span(&body, 1), before.Serializers, before.Context);
    }

    SavingWorld after;
    RestoreHeldRecords(after.Entities, saved, after.Serializers, after.Context);
    EXPECT_EQ(after.Index->RecordsOf(EntityId{}).size(), 1u);
    EXPECT_EQ(after.Index->AwaitedHolderOf(after.Resolve(bagId)), PersistentEntityId{ 0x10 });

    const EntityId body = after.Identified(PersistentEntityId{ 0x10 });
    EXPECT_EQ(after.Index->HolderOf(after.Resolve(bagId)), body);
}

TEST(HeldRecordSaves, CapturingGivesARecordMadeAtRunTimeAnId)
{
    SavingWorld world;
    const EntityId record = world.Entities.CreateEntity();
    world.Entities.AddComponent<Held>(record, Held{});
    ASSERT_EQ(world.Entities.TryGet<PersistentIdComponent>(record), nullptr);

    const JsonValue saved = CaptureHeldRecords(world.Entities, {}, world.Serializers, world.Context);

    ASSERT_NE(world.Entities.TryGet<PersistentIdComponent>(record), nullptr);
    EXPECT_EQ(saved.Find("records")->AsArray().front().Find("id")->AsString(),
              PersistentEntityIdToString(world.IdOf(record)));
}
