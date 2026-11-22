#include <abilities/AbilityKit.h>
#include <core/json/JsonParser.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <save/HeldRecordSaves.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kRoom{ 0x61 };
    constexpr ZoneId kHall{ 0x62 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "cellar" };
    constexpr PersistentEntityId kShelf{ 0x10 };

    struct Lines final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            (level == LogLevel::Error ? Errors : Warnings).emplace_back(message);
        }

        [[nodiscard]] std::size_t Mentioning(const std::vector<std::string>& lines, std::string_view text) const
        {
            return static_cast<std::size_t>(std::ranges::count_if(
                lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
        }

        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
    };

    WorldComponentSchema SchemaWith(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    EntityBuildPackage Room()
    {
        EntityBuildPackage package;
        const PackageEntityId shelf = package.CreateEntity();
        EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(shelf, { kShelf }));
        EXPECT_TRUE(package.SetPersistentId(shelf, kShelf));
        EXPECT_TRUE(package.AddComponent<LocalTransform>(shelf, LocalTransform{}));
        return package;
    }

    struct GainedWorld
    {
        GainedWorld()
            : Log(Logging.AddSink<Lines>())
            , Context(Logging)
            , Schema(SchemaWith(Serializers))
            , Runtime(Schema)
        {
            World& world = Entities();
            RegisterAbilityKit(world);
            InstallInventoryVocabulary(world);
            Arrows = world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                              ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
            world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
            Hero = world.CreateEntity();
            world.AddComponent<ItemContainer>(Hero, ItemContainer{});
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope(ZoneId zone = kRoom) { return Store().Scope(kLevel, zone); }
        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }
        PersistentEntityId IdOf(EntityId entity) { return Entities().TryGet<PersistentIdComponent>(entity)->Id; }
        std::vector<EntityId> Held(EntityId holder) { return ItemsIn(Entities(), holder); }
        StoragePartitionId PartitionOf(ZoneId zone) { return Runtime.FindZone(zone)->Partition; }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(ZoneId zone = kRoom)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, zone, Room(), Serializers, Context, kLogic, &error,
                                          Scope(zone)))
                << error.Message;
            FinishResidency();
        }

        void Detach(ZoneId zone = kRoom)
        {
            ASSERT_TRUE(Runtime.RequestDetach(zone));
            FinishResidency();
        }

        EntityId Drop(std::uint32_t count, Vec3d at, ZoneId zone = kRoom, bool keep = true)
        {
            Inventory inventory(Entities(), FixedSimTime{});
            const EntityId entry = inventory.Give(Hero, Arrows, count).Entry;
            Transform3f where = Transform3f::Identity();
            where.Position = at;
            const EntityId pickup = inventory.Drop(entry, where, PartitionOf(zone)).Entry;
            EXPECT_TRUE(pickup.IsValid());
            if (keep)
            {
                EXPECT_TRUE(KeepWithZone(Entities(), pickup));
            }
            SettleHeldRecords(Entities());
            return pickup;
        }

        [[nodiscard]] bool LiesAt(PersistentEntityId id, ZoneId zone, Vec3d at, std::uint32_t count)
        {
            const EntityId pickup = Resolve(id);
            if (!pickup.IsValid())
                return false;
            const LocalTransform* placed = Entities().TryGet<LocalTransform>(pickup);
            const std::vector<EntityId> held = Held(pickup);
            EXPECT_EQ(Entities().GetEntityPartition(pickup), PartitionOf(zone));
            EXPECT_TRUE(Entities().HasComponent<KeptWithZone>(pickup));
            EXPECT_TRUE(Entities().HasComponent<ItemPickup>(pickup));
            EXPECT_NE(placed, nullptr);
            EXPECT_EQ(held.size(), 1u);
            return placed != nullptr && placed->Value.Position.X == at.X && placed->Value.Position.Y == at.Y
                && placed->Value.Position.Z == at.Z && held.size() == 1u
                && Entities().TryGet<ItemEntry>(held[0])->Count == count
                && Entities().GetEntityPartition(held[0]) == PartitionOf(zone);
        }

        std::vector<GainedZoneMember> Members(ZoneId zone = kRoom)
        {
            for (const DurableLevelState& level : Store().ExportDurable())
                for (const DurableZoneState& state : level.Zones)
                    if (state.Zone == zone && state.Lists != nullptr)
                        for (const ZoneStateList& list : DecodeZoneLists(*state.Lists).value_or(std::vector<ZoneStateList>{}))
                            if (list.Name == kGainedZoneList)
                                return DecodeGainedList(list.Payload).value_or(std::vector<GainedZoneMember>{});
            return {};
        }

        LoggingProvider Logging;
        Lines& Log;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        ItemId Arrows;
        EntityId Hero;
    };

    class ZoneGained : public ::testing::Test, public GainedWorld
    {
    };

    const Vec3d kSpot{ 2.0, 0.5, -3.0 };
}

TEST(ZoneGainedForm, AWholeMemberIsItsIdThenItsRecordAsText)
{
    const ZoneListEntity entity{ PersistentEntityId{ PersistentEntityIdRuntimeBit | 7 },
                                 *JsonParse(R"({"ItemPickup":{}})") };
    const std::vector<std::byte> payload = EncodeWholeGainedMember(entity);
    const std::string text = R"({"ItemPickup":{}})";
    ASSERT_EQ(payload.size(), sizeof(std::uint64_t) + sizeof(std::uint32_t) + text.size());
    std::uint64_t id = 0;
    std::uint32_t length = 0;
    std::memcpy(&id, payload.data(), sizeof(id));
    std::memcpy(&length, payload.data() + sizeof(id), sizeof(length));
    EXPECT_EQ(id, entity.Id.Value);
    EXPECT_EQ(length, text.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(payload.data()) + sizeof(id) + sizeof(length), length), text);

    const std::optional<ZoneListEntity> read = DecodeWholeGainedMember(payload);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->Id, entity.Id);
    EXPECT_EQ(WholeGainedMemberId(payload), entity.Id);
    std::vector<std::byte> cut = payload;
    cut.pop_back();
    EXPECT_FALSE(DecodeWholeGainedMember(cut).has_value());
}

TEST_F(ZoneGained, ADroppedItemIsThereWithItsEntryAfterItsZoneStreamsOutAndBack)
{
    Import();
    const EntityId pickup = Drop(3, kSpot);
    const PersistentEntityId id = IdOf(pickup);
    EXPECT_FALSE(IsAuthoredPersistentEntityId(id));
    const EntityId entry = Held(pickup).at(0);

    Detach();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_TRUE(Entities().IsAlive(entry)) << "the entry waits for its pickup";

    Import();
    EXPECT_TRUE(LiesAt(id, kRoom, kSpot, 3)) << "the dropped item did not come back with its zone";
    EXPECT_EQ(Held(Resolve(id)), std::vector{ entry });
}

TEST_F(ZoneGained, ATakenItemIsNotBroughtBack)
{
    Import();
    const PersistentEntityId id = IdOf(Drop(3, kSpot));
    Detach();
    Import();
    ASSERT_TRUE(Resolve(id).IsValid());
    ASSERT_EQ(Inventory(Entities(), FixedSimTime{}).Take(Resolve(id), Hero).Refusal, InventoryRefusal::None);
    Detach();
    EXPECT_TRUE(Members().empty());

    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_EQ(Held(Hero).size(), 1u);
}

TEST_F(ZoneGained, ADestroyedGainedEntityIsNotBroughtBack)
{
    Import();
    const PersistentEntityId id = IdOf(Drop(3, kSpot));
    Detach();
    Import();
    Entities().DestroyEntity(Resolve(id));
    SettleHeldRecords(Entities());
    Detach();
    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
}

TEST_F(ZoneGained, AnAtRestoreGainedEntityGoesAtRestoreAndAnAtDetachOneIsNotKept)
{
    Import();
    const EntityId loot = Drop(2, kSpot);
    Entities().AddComponent<ZoneStateLifetime>(loot, ZoneStateLifetime{ ZoneStateForgetting::AtRestore });
    const PersistentEntityId lootId = IdOf(loot);
    const EntityId litter = Drop(1, Vec3d{ 1.0, 0.0, 1.0 });
    Entities().AddComponent<ZoneStateLifetime>(litter, ZoneStateLifetime{ ZoneStateForgetting::AtDetach });
    const PersistentEntityId litterId = IdOf(litter);
    Detach();

    Import();
    EXPECT_TRUE(LiesAt(lootId, kRoom, kSpot, 2)) << "kept across its zone's unload";
    EXPECT_FALSE(Resolve(litterId).IsValid()) << "forgotten when its zone unloads";
    EXPECT_TRUE(Members().empty()) << "and neither is in a save";
    Detach();

    Store().Install(Store().ExportDurable());
    Import();
    EXPECT_FALSE(Resolve(lootId).IsValid()) << "forgotten when a save is restored";
}

TEST_F(ZoneGained, APickupDroppedWithoutBeingKeptIsNotKept)
{
    Import();
    const EntityId pickup = Drop(3, kSpot, kRoom, false);
    const PersistentEntityId id = IdOf(pickup);
    const EntityId entry = Held(pickup).at(0);
    Detach();
    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_FALSE(Entities().IsAlive(entry)) << "nothing waits for a pickup no zone keeps";
}

TEST_F(ZoneGained, AGameThatKeepsNoLevelStateKeepsNothingAZoneGained)
{
    Entities().SetResource(ZoneStateSerialization{});
    Import();
    const EntityId pickup = Drop(3, kSpot);
    const PersistentEntityId id = IdOf(pickup);
    const EntityId entry = Held(pickup).at(0);
    Detach();
    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_FALSE(Entities().IsAlive(entry)) << "nothing waits for a pickup no zone keeps";
    EXPECT_TRUE(Members().empty());
}

TEST_F(ZoneGained, AMemberOfAFormThisBuildDoesNotMakeRoundTripsUntouchedBesideAKeptOne)
{
    const GainedZoneMember future{ "from_source", { std::byte{ 9 }, std::byte{ 8 }, std::byte{ 7 } } };
    const ZoneStateList list{ std::string(kGainedZoneList), kZoneListVersion, EncodeGainedList(std::span(&future, 1)) };
    Store().Install(std::vector{ DurableLevelState{
        kLevel, { DurableZoneState{ kRoom, EncodeZoneLists(std::span(&list, 1)), nullptr } } } });
    Import();
    EXPECT_EQ(Log.Mentioning(Log.Warnings, "from_source"), 1u);
    const PersistentEntityId id = IdOf(Drop(4, kSpot));
    Detach();

    const std::vector<GainedZoneMember> members = Members();
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(members[0], future) << "the member this build does not make is written back byte for byte";
    EXPECT_EQ(members[1].Form, kWholeGainedForm);
    EXPECT_EQ(WholeGainedMemberId(members[1].Payload), id);

    Import();
    EXPECT_TRUE(LiesAt(id, kRoom, kSpot, 4));
    EXPECT_EQ(Log.Mentioning(Log.Warnings, "from_source"), 2u);
    Detach();
    EXPECT_EQ(Members().front(), future);
}

TEST_F(ZoneGained, AMemberThatDoesNotReadWholeIsDroppedWithWhatItHeldAndReported)
{
    Import();
    const EntityId pickup = Drop(3, kSpot);
    const PersistentEntityId id = IdOf(pickup);
    const EntityId entry = Held(pickup).at(0);
    Detach();
    ASSERT_TRUE(Entities().IsAlive(entry));

    const GainedZoneMember broken{ std::string(kWholeGainedForm),
                                   EncodeWholeGainedMember({ id, *JsonParse(R"({"future_lock":{"pins":3}})") }) };
    const ZoneStateList list{ std::string(kGainedZoneList), kZoneListVersion, EncodeGainedList(std::span(&broken, 1)) };
    Store().Install(std::vector{ DurableLevelState{
        kLevel, { DurableZoneState{ kRoom, EncodeZoneLists(std::span(&list, 1)), nullptr } } } });
    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_FALSE(Entities().IsAlive(entry)) << "what it held went with it";
    EXPECT_EQ(Log.Mentioning(Log.Errors, PersistentEntityIdToString(id)), 1u);
    EXPECT_EQ(Log.Mentioning(Log.Errors, "future_lock"), 1u);
    EXPECT_TRUE(Store().GainedOf(Scope()).empty());
}

TEST_F(ZoneGained, AMemberNamingAnAuthoredIdIsRefused)
{
    const GainedZoneMember authored{ std::string(kWholeGainedForm),
                                     EncodeWholeGainedMember({ kShelf, *JsonParse(R"({"ItemPickup":{}})") }) };
    const ZoneStateList list{ std::string(kGainedZoneList), kZoneListVersion, EncodeGainedList(std::span(&authored, 1)) };
    Store().Install(std::vector{ DurableLevelState{
        kLevel, { DurableZoneState{ kRoom, EncodeZoneLists(std::span(&list, 1)), nullptr } } } });
    const std::size_t before = Entities().GetAliveEntities().size();
    Import();
    EXPECT_EQ(Entities().GetAliveEntities().size(), before + 1) << "only what the level authored";
    EXPECT_EQ(Log.Mentioning(Log.Errors, PersistentEntityIdToString(kShelf)), 1u);
}

TEST_F(ZoneGained, AClientNeitherMakesNorRecordsWhatAZoneGained)
{
    Import();
    const PersistentEntityId id = IdOf(Drop(3, kSpot));
    Detach();
    ASSERT_EQ(Members().size(), 1u);
    Entities().SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    Import();
    EXPECT_FALSE(Resolve(id).IsValid());
    Detach();
    EXPECT_EQ(Members().size(), 1u) << "a client's detach does not forget what the authority kept";
}

TEST_F(ZoneGained, ASaveHoldsWhatEveryZoneGainedAndAFreshWorldGetsItBackWhetherOrNotItsZoneWasLoaded)
{
    Import(kRoom);
    Import(kHall);
    const PersistentEntityId inRoom = IdOf(Drop(3, kSpot, kRoom));
    const PersistentEntityId inHall = IdOf(Drop(5, Vec3d{ -1.0, 0.0, 4.0 }, kHall));
    Detach(kHall);

    SettleHeldRecords(Entities());
    CaptureZoneState(Entities(), Scope(kRoom), IdentifiedEntitiesIn(Entities(), PartitionOf(kRoom)), ZoneCapture::Live);
    const std::vector<DurableLevelState> durable = Store().ExportDurable();
    const std::vector<ScopedHeldRecords> held = CaptureLevelHeldRecords(
        Entities(),
        [&](EntityId holder) {
            const RuntimeZoneRecord* record = Runtime.FindPartition(Entities().GetEntityPartition(holder));
            return record != nullptr ? record->StateScope : ZoneStateScope{};
        },
        Serializers, Context);
    ASSERT_EQ(held.size(), 2u) << "each zone's dropped entry is written under that zone";

    GainedWorld fresh;
    fresh.Store().Install(durable);
    for (const ScopedHeldRecords& records : held)
    {
        const LevelReference* level = Store().LevelOf(records.Scope.Level);
        ASSERT_NE(level, nullptr);
        RestoreLevelHeldRecords(fresh.Entities(), fresh.Store().Scope(*level, records.Scope.Zone), records.Records,
                                fresh.Serializers, fresh.Context);
    }
    fresh.Import(kRoom);
    EXPECT_TRUE(fresh.LiesAt(inRoom, kRoom, kSpot, 3)) << "written while its zone was loaded";
    fresh.Import(kHall);
    EXPECT_TRUE(fresh.LiesAt(inHall, kHall, Vec3d{ -1.0, 0.0, 4.0 }, 5)) << "written while its zone was away";
}
