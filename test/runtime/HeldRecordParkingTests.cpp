#include <abilities/AbilityKit.h>
#include <core/json/JsonValue.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/WorldComponentSchema.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <net/NetReplicationComponents.h>
#include <net/NetZoneScope.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>
#include <save/HeldRecordSaves.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kCellar{ 0x41 };
    constexpr PersistentEntityId kChest{ 0xC4E5 };

    struct Lines final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Error)
                Errors.emplace_back(message);
        }

        [[nodiscard]] bool Mention(std::string_view text) const
        {
            return std::ranges::any_of(Errors, [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }

        std::vector<std::string> Errors;
    };

    LevelReference LevelNamed(std::string name) { return LevelReference{ .Kind = LevelKind::Scene, .Name = std::move(name) }; }

    struct ParkingTables
    {
        explicit ParkingTables()
        {
            ComponentRegistrar registrar(&Schema, &Serializers, &Layout);
            RegisterEngineComponents(registrar);
            Schema.Seal();
            Layout.Seal();
        }

        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
    };

    struct ParkingWorld : ParkingTables
    {
        explicit ParkingWorld(bool keepsLevelState = true)
            : Log(Logging.AddSink<Lines>())
            , Context(Logging)
            , Runtime(Schema)
        {
            World& world = Entities();
            RegisterAbilityKit(world);
            InstallInventoryVocabulary(world);
            Coin = world.GetResource<ItemRegistry>().Define("asset://items/coin.sdata",
                                                            ItemDefinition{ .Name = "Coin", .StackMax = 1 });
            Arrows = world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                              ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
            Bag = world.GetResource<ItemRegistry>().Define("asset://items/bag.sdata", ItemDefinition{ .Name = "Bag" });
            if (keepsLevelState)
                world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        HeldRecordIndex& Index() { return Entities().GetResource<HeldRecordIndex>(); }
        ZoneStateScope ScopeIn(const LevelReference& level) { return Store().Scope(level, kCellar); }
        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }
        PersistentEntityId IdOf(EntityId entity) { return Entities().TryGet<PersistentIdComponent>(entity)->Id; }
        Inventory Items() { return Inventory(Entities(), FixedSimTime{}); }

        static EntityBuildPackage Cellar()
        {
            EntityBuildPackage package;
            const PackageEntityId chest = package.CreateEntity();
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(chest, { kChest }));
            EXPECT_TRUE(package.SetPersistentId(chest, kChest));
            EXPECT_TRUE(package.AddComponent<ItemContainer>(chest, ItemContainer{}));
            return package;
        }

        void Finish()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const LevelReference& level = LevelNamed("cellar"))
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kCellar, Cellar(), Serializers, Context, kLogic, &error,
                                          ScopeIn(level)))
                << error.Message;
            Finish();
        }

        void Detach()
        {
            ASSERT_TRUE(Runtime.RequestDetach(kCellar));
            Finish();
        }

        std::size_t LiveRecords()
        {
            std::size_t count = 0;
            Query<Read<::Held>> records(Entities());
            records.ForEachChunk([&](auto& view) { count += view.Count(); });
            return count;
        }

        std::vector<PersistentEntityId> HeldIds(EntityId holder)
        {
            std::vector<PersistentEntityId> ids;
            for (const EntityId record : ItemsIn(Entities(), holder))
                ids.push_back(IdOf(record));
            return ids;
        }

        LoggingProvider Logging;
        Lines& Log;
        SceneSerializationContext Context;
        RuntimeWorld Runtime;
        ItemId Coin;
        ItemId Arrows;
        ItemId Bag;
    };

    class HeldRecordParking : public ::testing::Test, public ParkingWorld
    {
    };

    struct ChestContents
    {
        std::vector<PersistentEntityId> InChest;
        PersistentEntityId BagId;
        std::vector<PersistentEntityId> InBag;
    };

    ChestContents Stock(ParkingWorld& world)
    {
        const EntityId chest = world.Resolve(kChest);
        Inventory items = world.Items();
        (void)items.Give(chest, world.Coin, 3);
        const EntityId bag = items.Give(chest, world.Bag, 1).Entry;
        world.Entities().AddComponent<ItemContainer>(bag, ItemContainer{});
        (void)items.Give(bag, world.Arrows, 9);
        world.Finish();
        return ChestContents{ world.HeldIds(chest), world.IdOf(bag), world.HeldIds(bag) };
    }

    void ExpectContents(ParkingWorld& world, const ChestContents& expected)
    {
        const EntityId chest = world.Resolve(kChest);
        ASSERT_TRUE(chest.IsValid());
        EXPECT_EQ(world.HeldIds(chest), expected.InChest);
        const EntityId bag = world.Resolve(expected.BagId);
        ASSERT_TRUE(bag.IsValid());
        EXPECT_EQ(world.HeldIds(bag), expected.InBag);
        ASSERT_EQ(expected.InBag.size(), 1u);
        EXPECT_EQ(world.Entities().TryGet<ItemEntry>(world.Resolve(expected.InBag[0]))->Count, 9u);
        EXPECT_EQ(world.Entities().GetEntityPartition(bag), world.Entities().GetEntityPartition(chest));
    }
}

TEST_F(HeldRecordParking, AChestsContentsLeaveTheWorldWithItsZoneAndComeBackWithIt)
{
    Import();
    const ChestContents stocked = Stock(*this);
    ASSERT_EQ(stocked.InChest.size(), 4u);
    const std::size_t records = LiveRecords();

    Detach();
    EXPECT_EQ(LiveRecords(), 0u) << "what a chest away holds is bytes in its zone's record, not entities";
    EXPECT_EQ(Index().Count(), 0u);

    Import();
    EXPECT_EQ(LiveRecords(), records);
    ExpectContents(*this, stocked);
}

TEST_F(HeldRecordParking, TwoThousandItemsAcrossTwentyLevelsLeaveOnlyTheLoadedLevelsRecordsAlive)
{
    std::vector<std::vector<PersistentEntityId>> stocked;
    for (int level = 0; level < 20; ++level)
    {
        const LevelReference named = LevelNamed("level_" + std::to_string(level));
        Import(named);
        ASSERT_TRUE(Items().Give(Resolve(kChest), Coin, 100).Entry.IsValid());
        Finish();
        stocked.push_back(HeldIds(Resolve(kChest)));
        ASSERT_EQ(stocked.back().size(), 100u);
        EXPECT_EQ(LiveRecords(), 100u) << "level " << level;
        Detach();
        EXPECT_EQ(LiveRecords(), 0u) << "level " << level;
    }

    Import(LevelNamed("level_7"));
    EXPECT_EQ(LiveRecords(), 100u);
    EXPECT_EQ(HeldIds(Resolve(kChest)), stocked[7]);
}

TEST_F(HeldRecordParking, AChestsContentsComeBackAfterLeavingAndReenteringItsLevel)
{
    Import(LevelNamed("cellar"));
    const ChestContents stocked = Stock(*this);
    Detach();
    SetAsideHeldRecords(Entities(), ScopeIn(LevelNamed("cellar")).Level);

    Import(LevelNamed("attic"));
    EXPECT_TRUE(HeldIds(Resolve(kChest)).empty()) << "a level sharing the chest's id has its own chest";
    Detach();
    ResumeHeldRecords(Entities(), ScopeIn(LevelNamed("cellar")).Level);

    Import(LevelNamed("cellar"));
    ExpectContents(*this, stocked);
}

TEST_F(HeldRecordParking, AChestsContentsComeBackInAFreshWorldFromASave)
{
    Import();
    const ChestContents stocked = Stock(*this);
    Detach();
    const std::vector<DurableLevelState> saved = Store().ExportDurable();

    ParkingWorld fresh;
    fresh.Store().Install(saved);
    fresh.Import();
    ExpectContents(fresh, stocked);

    fresh.Detach();
    fresh.Import();
    ExpectContents(fresh, stocked);
}

TEST_F(HeldRecordParking, WhatAResidentChestHoldsIsSavedBesideItsZoneAndComesBackInAFreshWorld)
{
    Import();
    const ChestContents stocked = Stock(*this);
    const std::vector<ScopedHeldRecords> saved = CaptureLevelHeldRecords(
        Entities(), [&](EntityId) { return ScopeIn(LevelNamed("cellar")); }, Serializers, Context);
    ASSERT_EQ(saved.size(), 1u);
    const ZoneStateList held{ std::string(kHeldZoneList), kZoneListVersion, EncodeEntityList(saved[0].Records) };

    ParkingWorld fresh;
    fresh.Store().Install(std::vector{ DurableLevelState{
        LevelNamed("cellar"), { DurableZoneState{ kCellar, nullptr, EncodeZoneLists(std::span(&held, 1)) } } } });
    fresh.Import();
    ExpectContents(fresh, stocked);
}

TEST_F(HeldRecordParking, ForgettingALevelDropsWhatItsChestsHeld)
{
    Import();
    (void)Stock(*this);
    Detach();
    Store().ForgetLevel(LevelNamed("cellar"));

    Import();
    EXPECT_TRUE(HeldIds(Resolve(kChest)).empty());
    EXPECT_EQ(LiveRecords(), 0u);
}

TEST_F(HeldRecordParking, ACancelledImportLosesNothing)
{
    Import();
    const ChestContents stocked = Stock(*this);
    Detach();

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackageHidden(Runtime, Schema, kCellar, Cellar(), Serializers, Context, &error,
                                        ScopeIn(LevelNamed("cellar"))))
        << error.Message;
    ASSERT_TRUE(Runtime.CancelZoneImport(kCellar));
    Finish();
    EXPECT_EQ(LiveRecords(), 0u);

    Import();
    ExpectContents(*this, stocked);
}

TEST(HeldRecordParkingAlive, ACancelledImportLosesNothingInAGameThatKeepsNoLevelState)
{
    ParkingWorld world(false);
    world.Import();
    const ChestContents stocked = Stock(world);
    world.Detach();
    ASSERT_EQ(world.LiveRecords(), 5u) << "without kept level state a chest away keeps live records";

    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackageHidden(world.Runtime, world.Schema, kCellar, ParkingWorld::Cellar(), world.Serializers,
                                        world.Context, &error, world.ScopeIn(LevelNamed("cellar"))))
        << error.Message;
    world.Finish();
    ASSERT_TRUE(world.Runtime.CancelZoneImport(kCellar));
    world.Finish();
    EXPECT_EQ(world.LiveRecords(), 5u) << "a cancelled import keeps what it was given";

    world.Import();
    ExpectContents(world, stocked);
}

TEST_F(HeldRecordParking, RecordsWhoseHolderDoesNotArriveAreDroppedNotLeftWaiting)
{
    Import();
    World& world = Entities();
    const EntityId pickup = world.CreateEntity(Runtime.FindZone(kCellar)->Partition);
    world.AddComponent<LocalTransform>(pickup, LocalTransform{});
    world.AddComponent<ItemContainer>(pickup, ItemContainer{});
    ASSERT_TRUE(KeepWithZone(world, pickup));
    const PersistentEntityId pickupId = IdOf(pickup);
    (void)Items().Give(pickup, Coin, 2);
    Finish();
    Detach();

    std::vector<DurableLevelState> saved = Store().ExportDurable();
    ASSERT_EQ(saved.size(), 1u);
    std::vector<ZoneStateList> lists = *DecodeZoneLists(*saved[0].Zones[0].Lists);
    const auto gained = std::ranges::find(lists, kGainedZoneList, &ZoneStateList::Name);
    ASSERT_NE(gained, lists.end());
    std::vector<GainedZoneMember> members = *DecodeGainedList(gained->Payload);
    ASSERT_EQ(members.size(), 1u);
    ZoneListEntity member = *DecodeWholeGainedMember(members[0].Payload);
    member.Value.AsObject().emplace_back("no_such_component", JsonValue(JsonValue::Object{}));
    members[0].Payload = EncodeWholeGainedMember(member);
    gained->Payload = EncodeGainedList(members);
    saved[0].Zones[0].Lists = EncodeZoneLists(lists);

    ParkingWorld fresh;
    fresh.Store().Install(saved);
    fresh.Import();
    EXPECT_FALSE(fresh.Resolve(pickupId).IsValid());
    EXPECT_EQ(fresh.LiveRecords(), 0u) << "nothing waits for a holder that will never come";
    EXPECT_TRUE(fresh.Index().AwaitingRecords().empty());
    EXPECT_TRUE(fresh.Log.Mention("did not arrive"));
}

TEST(HeldRecordParkingReplication, AClientThatHeldAChestsEntriesSeesThemGoAndReturn)
{
    ParkingWorld authority;
    World client;
    authority.Schema.Apply(client);
    ReplicationAuthorityIdentity identity;
    ReplicationChangeStore changes;
    ReplicationPeerState peer;
    ReplicationClientIdentity clientIdentity;
    std::vector<std::byte> scratch(256 * 1024);
    std::uint64_t generation = 0;
    std::uint64_t tick = 0;
    const std::vector<ZoneId> streamed{ kCellar };
    const auto replicate = [&] {
        changes.Update(authority.Entities(), authority.Layout, identity, ++generation, &authority.Runtime);
        SnapshotWriteRequest write;
        write.Changes = &changes;
        write.Layout = &authority.Layout;
        write.Peer = &peer;
        write.Tick = ++tick;
        write.Sequence = peer.NextSnapshotSequence();
        write.StreamedZones = streamed;
        write.OwnerPeer = 1;
        const SnapshotWriteResult written = ReplicationWriteSnapshot(write, scratch);
        ASSERT_TRUE(written.Ok);
        SnapshotApplyRequest apply;
        apply.Target = &client;
        apply.Schema = &authority.Schema;
        apply.Layout = &authority.Layout;
        apply.Identity = &clientIdentity;
        const SnapshotApplyResult applied = ReplicationApplySnapshot(apply, std::span(scratch).subspan(0, written.BytesWritten));
        ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
        NetSnapshotAck ack;
        ack.Observe(applied.Sequence);
        peer.Acknowledge(ack);
    };
    const auto clientRecords = [&] {
        std::size_t count = 0;
        Query<Read<::Held>> records(client);
        records.ForEachChunk([&](auto& view) { count += view.Count(); });
        return count;
    };
    (void)peer.Zones().Grant(kCellar);
    (void)peer.Zones().Acknowledge(kCellar);

    const auto share = [&] {
        authority.Entities().AddComponent<NetReplicated>(authority.Resolve(kChest));
        authority.Entities().AddComponent<NetOwner>(authority.Resolve(kChest), NetOwner{ .Peer = 1 });
    };
    authority.Import();
    share();
    (void)Stock(authority);
    replicate();
    ASSERT_EQ(clientRecords(), 5u);

    authority.Detach();
    replicate();
    EXPECT_EQ(clientRecords(), 0u) << "the entries went with their zone";

    authority.Import();
    share();
    replicate();
    EXPECT_EQ(clientRecords(), 5u) << "and came back with it";
}
