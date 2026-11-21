#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/ComponentPersistence.h>
#include <core/serialization/FourCC.h>
#include <ecs/World.h>
#include <ecs/EntityText.h>
#include <ecs/WorldComponentSchema.h>
#include <save/EngineSaveSections.h>
#include <save/HeldRecordSaves.h>
#include <world/ComponentRegistrar.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

struct RecordLabel
{
    int Value = 0;
};

template <>
struct TypeSchema<RecordLabel>
{
    static constexpr std::string_view Name = "record_label";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('R', 'L', 'B', 'L');
    static auto Fields() { return std::tuple{ MakeField("value", &RecordLabel::Value) }; }
};

template <>
struct ComponentPersistenceOf<RecordLabel>
{
    static constexpr ComponentPersistence Value = ComponentPersistence::Identity;
};

struct RecordWeight
{
    int Value = 0;
};

template <>
struct TypeSchema<RecordWeight>
{
    static constexpr std::string_view Name = "record_weight";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('R', 'W', 'G', 'T');
    static auto Fields() { return std::tuple{ MakeField("value", &RecordWeight::Value) }; }
};

namespace
{
    struct ErrorSink final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Error)
                Errors.emplace_back(message);
        }

        std::vector<std::string> Errors;
    };

    class UnwritableWeightSerializer final : public IComponentSerializer
    {
    public:
        ComponentTypeId TypeId() const override { return Inner.TypeId(); }
        std::string_view JsonKey() const override { return Inner.JsonKey(); }
        std::uint32_t BinaryChunkId() const override { return Inner.BinaryChunkId(); }
        std::span<const RuntimeField> RuntimeFields() const override { return Inner.RuntimeFields(); }
        std::vector<std::byte> DefaultBytes() const override { return Inner.DefaultBytes(); }
        void RegisterStorage(Registry& registry) const override { Inner.RegisterStorage(registry); }
        bool HasComponent(EntityId entity, const Registry& registry) const override
        {
            return Inner.HasComponent(entity, registry);
        }
        bool Save(IWriteArchive&, EntityId, const World&, SceneSerializationContext&) const override { return false; }
        bool Load(IReadArchive& archive, EntityId entity, Registry& registry,
                  SceneSerializationContext& context) override
        {
            return Inner.Load(archive, entity, registry, context);
        }
        bool LoadIntoWorld(IReadArchive& archive, EntityId entity, World& world,
                           SceneSerializationContext& context) override
        {
            return Inner.LoadIntoWorld(archive, entity, world, context);
        }
        bool Remove(EntityId entity, Registry& registry) const override { return Inner.Remove(entity, registry); }

    private:
        ComponentSerializer<RecordWeight> Inner;
    };

    struct SavingWorld
    {
        SavingWorld()
            : Context(Logging)
        {
            ComponentRegistrar registrar(&Schema, &Serializers, nullptr);
            registrar.Add<PersistentIdComponent>();
            registrar.Add<Held>();
            registrar.Add<RecordLabel>();
            Schema.Add<RecordWeight>();
            (void)Serializers.Register(std::make_unique<UnwritableWeightSerializer>());
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
    const EntityId stranger = world.Identified(PersistentEntityId{ PersistentEntityIdRuntimeBit | 0x20 });
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

TEST(HeldRecordSaves, WhatAnAuthoredHolderHoldsAndWhatWaitsIsSavedButNotWhatContentAuthored)
{
    SavingWorld world;
    const EntityId chest = world.Identified(PersistentEntityId{ 0xC4E57 });
    const EntityId authored = world.Entities.CreateEntity();
    world.Entities.AddComponent<PersistentIdComponent>(authored, PersistentIdComponent{ PersistentEntityId{ 0x5A0D } });
    world.Entities.AddComponent<Held>(authored, Held{ .Holder = { .Entity = chest, .Target = PersistentEntityId{ 0xC4E57 } } });
    const EntityId deposit = world.HeldBy(chest);
    const EntityId inAuthored = world.HeldBy(authored);
    const EntityId waiting = world.Entities.CreateEntity();
    world.Entities.AddComponent<Held>(waiting, Held{ .Holder = { .Entity = {}, .Target = PersistentEntityId{ 0x99 } } });

    const JsonValue saved = CaptureHeldRecords(world.Entities, {}, world.Serializers, world.Context);

    std::vector<std::string> ids;
    for (const JsonValue& record : saved.Find("records")->AsArray())
        ids.push_back(record.Find("id")->AsString());
    std::ranges::sort(ids);
    std::vector<std::string> expected{ PersistentEntityIdToString(world.IdOf(deposit)),
                                       PersistentEntityIdToString(world.IdOf(inAuthored)),
                                       PersistentEntityIdToString(world.IdOf(waiting)) };
    std::ranges::sort(expected);
    EXPECT_EQ(ids, expected) << "the authored record comes back with its zone, not from the save";
}

TEST(HeldRecordSaves, ARecordsSavedFormLeavesOutWhatItIsKnownBy)
{
    SavingWorld world;
    ErrorSink& errors = world.Logging.AddSink<ErrorSink>();
    const EntityId record = world.HeldBy(EntityId{});
    world.Entities.AddComponent<RecordLabel>(record, RecordLabel{ .Value = 3 });

    const JsonValue saved = CaptureHeldRecords(world.Entities, {}, world.Serializers, world.Context);

    ASSERT_EQ(saved.Find("records")->AsArray().size(), 1u);
    const JsonValue& entry = saved.Find("records")->AsArray().front();
    std::vector<std::string> keys;
    for (const auto& [key, value] : entry.Find("components")->AsObject())
        keys.push_back(key);
    EXPECT_EQ(keys, std::vector<std::string>{ "Held" }) << "identity travels beside the record, not in it";
    EXPECT_EQ(entry.Find("id")->AsString(), PersistentEntityIdToString(world.IdOf(record)));
    EXPECT_TRUE(errors.Errors.empty());
}

TEST(HeldRecordSaves, AComponentThatCannotBeWrittenIsLoggedAndLeftOutOfTheRecord)
{
    SavingWorld world;
    ErrorSink& errors = world.Logging.AddSink<ErrorSink>();
    const EntityId record = world.HeldBy(EntityId{});
    world.Entities.AddComponent<RecordWeight>(record, RecordWeight{ .Value = 4 });

    const JsonValue saved = CaptureHeldRecords(world.Entities, {}, world.Serializers, world.Context);

    const JsonValue& entry = saved.Find("records")->AsArray().front();
    EXPECT_EQ(entry.Find("components")->Find("record_weight"), nullptr);
    EXPECT_NE(entry.Find("components")->Find("Held"), nullptr);
    ASSERT_EQ(errors.Errors.size(), 1u);
    EXPECT_NE(errors.Errors[0].find("record_weight"), std::string::npos) << errors.Errors[0];
}

TEST(HeldRecordSaves, ADescriptionSaysARecordIsKeptWholeWithWhatItIsKnownByBeside)
{
    SavingWorld world;
    const EntityId record = world.HeldBy(EntityId{});
    world.Entities.AddComponent<RecordLabel>(record, RecordLabel{ .Value = 3 });
    const EntityId chest = world.Identified(PersistentEntityId{ 0xC4E57 });
    std::string recordName;
    AppendEntityText(recordName, record);
    std::string chestName;
    AppendEntityText(chestName, chest);

    EXPECT_EQ(DescribeSavedEntity(world.Entities, record, world.Serializers),
              (std::vector<std::string>{
                  recordName + " is a held record made at run time: a save keeps it whole, its identity beside it",
                  "  persistent_id: identity, beside the record", "  Held: saved, in the record",
                  "  record_label: identity, beside the record" }));
    EXPECT_EQ(DescribeSavedEntity(world.Entities, chest, world.Serializers),
              (std::vector<std::string>{ chestName + " is neither the local body nor a held record made at run time: "
                                                     "a save holds only whether its zone destroyed it and what that "
                                                     "zone's registered memories keep",
                                         "  persistent_id: identity" }));
}
