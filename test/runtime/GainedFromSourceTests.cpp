#include "SmapSceneFixture.h"

#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityKit.h>
#include <app/GameContexts.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/Serialize.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <jobs/AsyncTaskQueue.h>
#include <math/MathSchemas.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <projectile/Projectile.h>
#include <projectile/ProjectileState.h>
#include <projectile/ProjectileSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <runtime/spawn/GainedMemberSpawns.h>
#include <runtime/spawn/NetPrefabSpawner.h>
#include <runtime/spawn/SceneSpawnOverlay.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/GainedSpawnRequests.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = false, .Logic = true, .Audio = false };
    constexpr ZoneId kRoom{ 0x71 };
    const LevelReference kLevel{ .Kind = LevelKind::World, .Name = "armoury" };
    constexpr PersistentEntityId kFloor{ 0x10 };
    constexpr PersistentEntityId kShooter{ 0x20 };
    constexpr PersistentEntityId kNearBox{ 0x30 };
    constexpr PersistentEntityId kFarBox{ 0x40 };
    constexpr AssetId kCrate{ 0xC1A7E00000000001ull };
    constexpr AssetId kGrenade{ 0x6E7ADE0000000001ull };
    constexpr AssetId kBullet{ 0xB011E70000000001ull };
    constexpr double kDt = 0.1;

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

    JsonValue Placed(Vec3d at)
    {
        Transform3f place = Transform3f::Identity();
        place.Position = at;
        return JsonValue(JsonValue::Object{ { "local", ToJson(place) } });
    }

    SmapContents Crate(float speed)
    {
        SmapContents contents;
        SmapEntityRecord root;
        root.Components.emplace_back(MakeComponentTypeId("Transform"), Placed(Vec3d::Zero()));
        root.Components.emplace_back(ResolveComponentTypeId<LookOrientation>(), *JsonParse(R"({"yaw":0})"));
        root.Components.emplace_back(ResolveComponentTypeId<Projectile>(),
                                     JsonValue(JsonValue::Object{ { "speed", JsonValue(static_cast<double>(speed)) } }));
        contents.Entities.push_back(std::move(root));
        SmapEntityRecord lid;
        lid.Parent = 0;
        lid.Components.emplace_back(MakeComponentTypeId("Transform"), Placed(Vec3d{ 0.0, 0.5, 0.0 }));
        contents.Entities.push_back(std::move(lid));
        return contents;
    }

    SmapContents Shot(bool kept)
    {
        SmapContents contents;
        SmapEntityRecord root;
        root.Components.emplace_back(MakeComponentTypeId("Transform"), Placed(Vec3d::Zero()));
        root.Components.emplace_back(ResolveComponentTypeId<Projectile>(), *JsonParse(std::string(R"({
            "speed": 10, "radius": 0.1, "lifetime": 5, "max_hits": 4,
            "passes_through": "Test.Penetrable", "impact_cue": "Cue.Test.Impact", "kept": )")
                                                                                  + (kept ? "true" : "false") + "}"));
        contents.Entities.push_back(std::move(root));
        SmapEntityRecord fuse;
        fuse.Parent = 0;
        fuse.Components.emplace_back(MakeComponentTypeId("Transform"), Placed(Vec3d{ -0.1, 0.0, 0.0 }));
        contents.Entities.push_back(std::move(fuse));
        return contents;
    }

    EntityBuildPackage Room()
    {
        EntityBuildPackage package;
        for (const PersistentEntityId id : { kFloor, kShooter, kNearBox, kFarBox })
        {
            const PackageEntityId entity = package.CreateEntity();
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
            EXPECT_TRUE(package.SetPersistentId(entity, id));
            EXPECT_TRUE(package.AddComponent<LocalTransform>(entity, LocalTransform{}));
        }
        return package;
    }

    struct SourceWorld
    {
        explicit SourceWorld(std::uint64_t firstTick = 0, float crateSpeed = 10.0f, bool cratePublished = true)
            : Log(Logging.AddSink<Lines>())
            , Context(Logging)
            , Schema(SchemaWith(Serializers))
            , Runtime(Schema)
            , Tasks(0)
            , Registry(Logging)
            , Scenes(Logging)
            , SceneLoader(Logging, &Scenes, &Serializers)
            , Assets(Logging, Registry)
            , Spawns(Runtime, Schema, Serializers, Tasks, Logging)
            , Gained(Runtime, Spawns, Logging)
            , Prefabs(Schema, Serializers, Logging)
            , Tick(firstTick)
        {
            RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
            Spawns.ConnectAssets(&Assets, &Scenes);
            Prefabs.ConnectAssets(&Assets, &Scenes);
            const std::string test = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            if (cratePublished)
                Scenes_.push_back(
                    std::make_unique<TempSmapScene>(Registry, Serializers, Crate(crateSpeed), test + "_crate", kCrate));
            Scenes_.push_back(std::make_unique<TempSmapScene>(Registry, Serializers, Shot(true), test + "_grenade", kGrenade));
            Scenes_.push_back(std::make_unique<TempSmapScene>(Registry, Serializers, Shot(false), test + "_bullet", kBullet));
            World& world = Entities();
            RegisterAbilityKit(world);
            InstallInventoryVocabulary(world);
            Penetrable = *world.GetResource<GameplayTagRegistry>().RegisterTag("Test.Penetrable");
            Impact = *world.GetResource<GameplayTagRegistry>().RegisterTag("Cue.Test.Impact");
            Arrows = world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata",
                                                              ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
            KeepLevelState();
            world.SetResource(ZonePlacement{ .PartitionAt = [this](const Vec3d&) -> std::optional<StoragePartitionId> {
                const RuntimeZoneRecord* record = Runtime.FindZone(kRoom);
                return record != nullptr ? std::optional(record->Partition) : std::nullopt;
            } });
            System = std::make_unique<ProjectileSystem>(
                world, ProjectileSystem::Services{ .Physics = &Physics, .Prefabs = &Prefabs, .Logging = &Logging });
        }

        void KeepLevelState()
        {
            Entities().SetResource(ZoneStateSerialization{
                .Serializers = &Serializers, .Context = &Context, .SourcePackage = [this](AssetId source) {
                    return SpawnSourcePackage(Assets, Scenes, Serializers, source);
                } });
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope() { return Store().Scope(kLevel, kRoom); }
        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }
        PersistentEntityId IdOf(EntityId entity) { return Entities().TryGet<PersistentIdComponent>(entity)->Id; }
        StoragePartitionId Partition() { return Runtime.FindZone(kRoom)->Partition; }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import()
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, kRoom, Room(), Serializers, Context, kLogic, &error, Scope()))
                << error.Message;
            FinishResidency();
        }

        void Detach()
        {
            ASSERT_TRUE(Runtime.RequestDetach(kRoom));
            FinishResidency();
        }

        void Drain()
        {
            Gained.Request();
            (void)Tasks.PumpWork();
            (void)Tasks.DrainCompletions();
            Spawns.Pump();
            Gained.Settle();
        }

        EntityId SpawnCrate(Vec3d at)
        {
            Transform3f root = Transform3f::Identity();
            root.Position = at;
            const SceneSpawnId spawn = Spawns.RequestSpawn(kCrate, root, Partition());
            Drain();
            const EntityId crate = Spawns.Root(spawn);
            EXPECT_TRUE(KeepWithZone(Entities(), crate));
            return crate;
        }

        std::vector<GainedZoneMember> Members()
        {
            for (const DurableLevelState& level : Store().ExportDurable())
                for (const DurableZoneState& state : level.Zones)
                    if (state.Zone == kRoom && state.Lists != nullptr)
                        for (const ZoneStateList& list :
                             DecodeZoneLists(*state.Lists).value_or(std::vector<ZoneStateList>{}))
                            if (list.Name == kGainedZoneList)
                                return DecodeGainedList(list.Payload).value_or(std::vector<GainedZoneMember>{});
            return {};
        }

        std::vector<EntityId> GroupOf(EntityId root)
        {
            const SceneInstance* instance = Entities().TryGet<SceneInstance>(root);
            if (instance == nullptr)
                return {};
            const std::span<const EntityId> members = Entities().GetResource<SceneInstanceIndex>().Entities(instance->Id);
            return { members.begin(), members.end() };
        }

        EntityId ChildOf(EntityId root)
        {
            for (const EntityId member : GroupOf(root))
                if (const Parent* parent = Entities().TryGet<Parent>(member); parent != nullptr && parent->Entity == root)
                    return member;
            return {};
        }

        void Box(PersistentEntityId id, Vec3d centre)
        {
            const EntityId entity = Resolve(id);
            ASSERT_TRUE(entity.IsValid());
            Entities().AddComponent<GameplayTagContainer>(entity, GameplayTagContainer{});
            (void)Entities().TryGet<GameplayTagContainer>(entity)->Grant(Penetrable);
            BodyDesc body;
            body.Shape = CollisionShape::MakeBox(Vec3d{ 0.2, 1.0, 1.0 });
            body.Position = centre;
            body.Motion = BodyMotion::Static;
            body.Layer = CollisionLayer::Static;
            body.UserData = PackEntity(entity);
            (void)Physics.AddBody(body);
        }

        EntityId Shooter()
        {
            const EntityId shooter = Resolve(kShooter);
            if (!Entities().HasComponent<AbilityCueLog>(shooter))
                Entities().AddComponent<AbilityCueLog>(shooter, AbilityCueLog{});
            return shooter;
        }

        std::size_t Cues()
        {
            std::size_t count = 0;
            for (const AbilityCueEntry& entry : Entities().TryGet<AbilityCueLog>(Shooter())->Entries)
                count += entry.Seq != 0 && entry.Cue == Impact ? 1u : 0u;
            return count;
        }

        void Fly(int ticks)
        {
            StoragePartitionSet partitions;
            partitions.Add(PersistentStoragePartition);
            if (const RuntimeZoneRecord* record = Runtime.FindZone(kRoom))
                partitions.Add(record->Partition);
            for (int i = 0; i < ticks; ++i)
            {
                ++Tick;
                FixedLogicContext fixed{ .Config = Config,
                                         .Runtime = Frames,
                                         .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = Tick },
                                         .Entities = Entities(),
                                         .Partitions = partitions };
                System->FixedLogic(fixed);
            }
        }

        EntityId Launch(AssetId prefab)
        {
            ProjectileLaunchResult result = ProjectileLaunchResult::Refused;
            const EntityId shot = System->Launch(ProjectileLaunch{ .Prefab = prefab,
                                                                   .Instigator = Shooter(),
                                                                   .Origin = Vec3d::Zero(),
                                                                   .Direction = Vec3d(1.0f, 0.0f, 0.0f),
                                                                   .Tick = Tick + 1 },
                                                 &result);
            EXPECT_EQ(result, ProjectileLaunchResult::Launched);
            return shot;
        }

        LoggingProvider Logging;
        Lines& Log;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        AsyncTaskQueue Tasks;
        AssetRegistry Registry;
        SceneCache Scenes;
        SceneAssetLoader SceneLoader;
        AssetSystem Assets;
        SceneSpawnService Spawns;
        GainedMemberSpawns Gained;
        NetPrefabSpawner Prefabs;
        PhysicsWorld Physics;
        EngineConfig Config;
        RuntimeFrameLoop Frames;
        std::unique_ptr<ProjectileSystem> System;
        std::vector<std::unique_ptr<TempSmapScene>> Scenes_;
        GameplayTagId Penetrable;
        GameplayTagId Impact;
        ItemId Arrows;
        std::uint64_t Tick = 0;
    };

    const Vec3d kLeftAt{ 4.0, 0.0, 5.0 };

    JsonValue SavedJson(const GainedZoneMember& member)
    {
        const std::optional<ZoneListEntity> saved = DecodeGainedMember(member.Payload);
        return saved.has_value() ? saved->Value : JsonValue();
    }

    void ExpectCrateBack(SourceWorld& world, PersistentEntityId id, float speed)
    {
        const EntityId crate = world.Resolve(id);
        ASSERT_TRUE(crate.IsValid()) << "the member did not come back";
        EXPECT_TRUE(world.Entities().HasComponent<KeptWithZone>(crate));
        EXPECT_EQ(world.Entities().GetEntityPartition(crate), world.Partition());
        ASSERT_NE(world.Entities().TryGet<LookOrientation>(crate), nullptr);
        ASSERT_NE(world.Entities().TryGet<Projectile>(crate), nullptr);
        EXPECT_FLOAT_EQ(world.Entities().TryGet<LookOrientation>(crate)->Yaw, 1.5f) << "what the save changed";
        EXPECT_FLOAT_EQ(world.Entities().TryGet<Projectile>(crate)->Speed, speed) << "what the prefab says";
        EXPECT_EQ(world.Entities().TryGet<LocalTransform>(crate)->Value.Position, kLeftAt);
        const EntityId lid = world.ChildOf(crate);
        ASSERT_TRUE(lid.IsValid()) << "its child did not come back with it";
        EXPECT_EQ(world.Entities().GetEntityPartition(lid), world.Partition());
    }
}

TEST(GainedFromSource, AMarkedPrefabSpawnReturnsWithItsChildrenAfterItsZonesRoundTripAndASave)
{
    SourceWorld world;
    world.Import();
    const EntityId crate = world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 });
    ASSERT_TRUE(world.ChildOf(crate).IsValid());
    const PersistentEntityId id = world.IdOf(crate);
    world.Entities().TryGet<LookOrientation>(crate)->Yaw = 1.5f;
    world.Entities().TryGet<LocalTransform>(crate)->Value.Position = kLeftAt;

    world.Detach();
    const std::vector<GainedZoneMember> members = world.Members();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(members[0].Form, kSourceGainedForm);
    EXPECT_EQ(GainedMemberId(members[0].Payload), id);
    const JsonValue saved = SavedJson(members[0]);
    ASSERT_TRUE(saved.IsObject());
    ASSERT_NE(saved.Find("source"), nullptr);
    EXPECT_EQ(saved.Find("source")->AsString(), AssetIdToString(kCrate));
    ASSERT_NE(saved.Find("patch"), nullptr);
    EXPECT_NE(saved.Find("patch")->Find("LookOrientation"), nullptr);
    EXPECT_EQ(saved.Find("patch")->Find("Projectile"), nullptr) << "what the prefab says is not saved";

    world.Import();
    EXPECT_FALSE(world.Resolve(id).IsValid()) << "a member from its source lands after its zone, as a body does";
    world.Drain();
    ExpectCrateBack(world, id, 10.0f);

    SourceWorld fresh(5000);
    fresh.Store().Install(world.Store().ExportDurable());
    fresh.Import();
    fresh.Drain();
    ExpectCrateBack(fresh, id, 10.0f);
}

TEST(GainedFromSource, ARetunedPrefabReachesIt)
{
    SourceWorld world;
    world.Import();
    const EntityId crate = world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 });
    const PersistentEntityId id = world.IdOf(crate);
    world.Entities().TryGet<LookOrientation>(crate)->Yaw = 1.5f;
    world.Entities().TryGet<LocalTransform>(crate)->Value.Position = kLeftAt;
    world.Detach();

    SourceWorld retuned(0, 25.0f);
    retuned.Store().Install(world.Store().ExportDurable());
    retuned.Import();
    retuned.Drain();
    ExpectCrateBack(retuned, id, 25.0f);
}

TEST(GainedFromSource, AMemberWhoseSourceStopsResolvingIsWrittenWholeWithAnError)
{
    SourceWorld world;
    world.Import();
    const PersistentEntityId id = world.IdOf(world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 }));
    world.Entities().GetResource<ZoneStateSerialization>().SourcePackage = [](AssetId) {
        return std::optional<EntityBuildPackage>{};
    };
    world.Detach();

    const std::vector<GainedZoneMember> members = world.Members();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(members[0].Form, kWholeGainedForm);
    EXPECT_EQ(GainedMemberId(members[0].Payload), id);
    EXPECT_EQ(world.Log.Mentioning(world.Log.Errors, PersistentEntityIdToString(id)), 1u);
    EXPECT_EQ(world.Log.Mentioning(world.Log.Errors, "children"), 1u);
}

TEST(GainedFromSource, WhatAMemberFromItsSourceHoldsWaitsForItToLandEvenAcrossItsZonesDetach)
{
    SourceWorld world;
    world.Import();
    const EntityId crate = world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 });
    world.Entities().AddComponent<ItemContainer>(crate, ItemContainer{});
    const PersistentEntityId id = world.IdOf(crate);
    (void)Inventory(world.Entities(), FixedSimTime{}).Give(crate, world.Arrows, 3);
    SettleHeldRecords(world.Entities());
    ASSERT_EQ(ItemsIn(world.Entities(), crate).size(), 1u);
    world.Detach();

    world.Import();
    EXPECT_EQ(world.Log.Mentioning(world.Log.Errors, "did not arrive"), 0u);
    EXPECT_EQ(world.Entities().GetResource<HeldRecordIndex>().AwaitingRecords().size(), 1u);
    world.Detach();
    EXPECT_EQ(world.Members().size(), 1u) << "a member not landed yet is still the zone's";

    world.Import();
    world.Drain();
    const EntityId back = world.Resolve(id);
    ASSERT_TRUE(back.IsValid());
    const std::vector<EntityId> held = ItemsIn(world.Entities(), back);
    ASSERT_EQ(held.size(), 1u) << "what it held did not wait for it";
    EXPECT_EQ(world.Entities().TryGet<ItemEntry>(held[0])->Count, 3u);
    EXPECT_EQ(world.Log.Mentioning(world.Log.Errors, "did not arrive"), 0u);
}

TEST(GainedFromSource, WhatAMemberHoldsIsDroppedAndReportedWhenItsSourceDoesNotSpawn)
{
    SourceWorld world;
    world.Import();
    const EntityId crate = world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 });
    world.Entities().AddComponent<ItemContainer>(crate, ItemContainer{});
    const PersistentEntityId id = world.IdOf(crate);
    (void)Inventory(world.Entities(), FixedSimTime{}).Give(crate, world.Arrows, 3);
    SettleHeldRecords(world.Entities());
    world.Detach();

    SourceWorld unpublished(0, 10.0f, false);
    unpublished.Store().Install(world.Store().ExportDurable());
    unpublished.Import();
    unpublished.Drain();
    EXPECT_FALSE(unpublished.Resolve(id).IsValid());
    EXPECT_TRUE(unpublished.Entities().GetResource<HeldRecordIndex>().AwaitingRecords().empty());
    EXPECT_EQ(unpublished.Log.Mentioning(unpublished.Log.Errors, "did not spawn"), 1u);
    EXPECT_EQ(unpublished.Log.Mentioning(unpublished.Log.Errors, "did not arrive"), 1u);
    EXPECT_TRUE(unpublished.Store().GainedOf(unpublished.Scope()).empty());
}

TEST(GainedFromSource, AMemberFromItsSourceIsFramedSoABuildThatDoesNotMakeItCarriesIt)
{
    SourceWorld world;
    world.Import();
    (void)world.SpawnCrate(Vec3d{ 2.0, 0.0, 3.0 });
    world.Detach();
    const std::vector<GainedZoneMember> members = world.Members();
    ASSERT_EQ(members.size(), 1u);
    const std::vector<std::byte> list = EncodeGainedList(members);
    const std::optional<std::vector<GainedZoneMember>> read = DecodeGainedList(list);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, members);
    EXPECT_EQ(EncodeGainedList(*read), list) << "a member is carried byte for byte by its length and form name";
}

TEST(KeptProjectile, AKeptProjectileInTheAirAtASaveFliesOnAfterALoadAndDoesNotStrikeAgainWhatItStruck)
{
    SourceWorld world;
    world.Import();
    world.Box(kNearBox, Vec3d{ 1.0, 0.0, 0.0 });
    const EntityId shot = world.Launch(kGrenade);
    ASSERT_TRUE(world.Entities().HasComponent<KeptWithZone>(shot));
    EXPECT_EQ(world.Entities().GetEntityPartition(shot), world.Partition()) << "it flies in the zone it left from";
    world.Fly(3);
    ASSERT_EQ(world.Cues(), 1u) << "it did not strike the near box";
    const PersistentEntityId id = world.IdOf(shot);
    const ProjectileMotion before = *world.Entities().TryGet<ProjectileMotion>(shot);
    const Vec3d at = world.Entities().TryGet<LocalTransform>(shot)->Value.Position;
    world.Detach();

    const std::vector<GainedZoneMember> members = world.Members();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(members[0].Form, kSourceGainedForm);
    const std::string text = JsonStringify(SavedJson(members[0]));
    EXPECT_EQ(text.find("tick"), std::string::npos) << "a saved form holds no tick: " << text;

    SourceWorld fresh(7000);
    fresh.Store().Install(world.Store().ExportDurable());
    fresh.Import();
    fresh.Box(kNearBox, Vec3d{ 4.0, 0.0, 0.0 });
    fresh.Box(kFarBox, Vec3d{ 6.0, 0.0, 0.0 });
    fresh.Drain();
    const EntityId back = fresh.Resolve(id);
    ASSERT_TRUE(back.IsValid()) << "the projectile did not come back";
    EXPECT_TRUE(fresh.ChildOf(back).IsValid());
    const ProjectileMotion* motion = fresh.Entities().TryGet<ProjectileMotion>(back);
    ASSERT_NE(motion, nullptr);
    EXPECT_EQ(motion->Velocity, before.Velocity);
    EXPECT_FLOAT_EQ(motion->Remaining, before.Remaining);
    EXPECT_EQ(motion->HitCount, before.HitCount);
    EXPECT_EQ(motion->Hits[0], fresh.Resolve(kNearBox));
    ASSERT_NE(fresh.Entities().TryGet<ProjectileFlight>(back), nullptr);
    EXPECT_EQ(fresh.Entities().TryGet<ProjectileFlight>(back)->Instigator, fresh.Shooter());
    EXPECT_EQ(fresh.Entities().TryGet<LocalTransform>(back)->Value.Position, at);

    fresh.Fly(1);
    EXPECT_EQ(fresh.Entities().TryGet<ProjectileFlight>(back)->LaunchTick, 7001u) << "launched the tick it loaded on";
    fresh.Fly(4);
    EXPECT_EQ(fresh.Cues(), 1u) << "it struck the far box for its instigator and not the near one again";
    const ProjectileMotion* after = fresh.Entities().TryGet<ProjectileMotion>(back);
    ASSERT_NE(after, nullptr) << "it still flies";
    EXPECT_EQ(after->HitCount, 2u);
    EXPECT_EQ(after->Hits[1], fresh.Resolve(kFarBox));
}

TEST(KeptProjectile, AProjectileWhosePrefabDoesNotSayKeptIsNeverMarkedAndGetsNoId)
{
    SourceWorld world;
    world.Import();
    const EntityId shot = world.Launch(kBullet);
    EXPECT_FALSE(world.Entities().HasComponent<KeptWithZone>(shot));
    EXPECT_FALSE(world.Entities().HasComponent<PersistentIdComponent>(shot));
    EXPECT_EQ(world.Entities().GetEntityPartition(shot), world.Entities().GetEntityPartition(world.Shooter()));
    world.Fly(2);
    world.Detach();
    EXPECT_TRUE(world.Members().empty());
}

TEST(KeptProjectile, AKeptProjectileInAGameThatKeepsNoLevelStateIsNeverMarked)
{
    SourceWorld world;
    world.Entities().SetResource(ZoneStateSerialization{});
    world.Import();
    const EntityId shot = world.Launch(kGrenade);
    EXPECT_FALSE(world.Entities().HasComponent<KeptWithZone>(shot));
    EXPECT_FALSE(world.Entities().HasComponent<PersistentIdComponent>(shot));
}
