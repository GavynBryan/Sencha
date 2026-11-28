#include <abilities/AbilityKit.h>
#include <controller/LookOrientation.h>
#include <app/LevelCarry.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <logic/Claims.h>
#include <movement/components/CharacterFacts.h>
#include <logic/decision/DecisionAgent.h>
#include <navigation/NavAgent.h>
#include <navigation/NavAgentRequests.h>
#include <perception/ObservationMemory.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentRegistrar.h>
#include <world/LevelLandmark.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/held/HeldRecords.h>
#include <world/relocation/RelocationRules.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateLifetime.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneParticipation kLogic{ .Visible = false, .Physics = true, .Logic = true, .Audio = false };
    constexpr ZoneId kRoom{ 0x81 };
    constexpr ZoneId kHall{ 0x82 };
    const LevelReference kCellar{ .Kind = LevelKind::Scene, .Name = "cellar" };
    const LevelReference kAttic{ .Kind = LevelKind::Scene, .Name = "attic" };
    constexpr PersistentEntityId kLiftHere{ 0x10 };
    constexpr PersistentEntityId kCrate{ 0x20 };
    constexpr PersistentEntityId kGuard{ 0x30 };
    constexpr PersistentEntityId kShelf{ 0x40 };
    constexpr PersistentEntityId kBook{ 0x41 };
    constexpr PersistentEntityId kHex{ 0x50 };
    constexpr PersistentEntityId kLiftThere{ 0x60 };
    constexpr PersistentEntityId kHeroId{ PersistentEntityIdRuntimeBit | 0x99 };
    const Vec3d kLiftAt{ 6.0, 0.0, -8.0 };
    const Vec3d kCrateAt{ 6.0, 0.5, -6.0 };
    const Vec3d kGuardAt{ 5.0, 0.0, -7.0 };
    const Vec3d kThereAt{ 20.0, 0.0, 4.0 };

    struct Lines final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Error)
                Errors.emplace_back(message);
        }
        std::vector<std::string> Errors;
    };

    WorldComponentSchema SchemaWith(ComponentSerializerRegistry& serializers)
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    Transform3f At(Vec3d position, Quatf rotation = Quatf::Identity())
    {
        return Transform3f{ position, rotation, Vec3d::One() };
    }

    PackageEntityId Placed(EntityBuildPackage& package, PersistentEntityId id, const Transform3f& place)
    {
        const PackageEntityId entity = package.CreateEntity();
        EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { id }));
        EXPECT_TRUE(package.SetPersistentId(entity, id));
        EXPECT_TRUE(package.AddComponent<LocalTransform>(entity, LocalTransform{ place }));
        return entity;
    }

    void Landmark(EntityBuildPackage& package, PersistentEntityId id, const Transform3f& place)
    {
        const PackageEntityId entity = Placed(package, id, place);
        LevelLandmark landmark;
        landmark.Name.Assign("lift");
        EXPECT_TRUE(package.AddComponent<LevelLandmark>(entity, landmark));
    }

    EntityBuildPackage Room()
    {
        EntityBuildPackage package;
        Landmark(package, kLiftHere, At(kLiftAt));
        Placed(package, kCrate, At(kCrateAt));
        Placed(package, kGuard, At(kGuardAt));
        const PackageEntityId shelf = Placed(package, kShelf, At(Vec3d{ -3.0, 0.0, 0.0 }));
        const PackageEntityId book = Placed(package, kBook, At(Vec3d{ 0.0, 1.0, 0.0 }));
        EXPECT_TRUE(package.SetParent(book, shelf));
        Placed(package, kHex, At(Vec3d{ 0.0, 0.0, 9.0 }));
        return package;
    }

    const Quatf kQuarterTurn{ 0.0f, 0.70710678f, 0.0f, 0.70710678f };

    EntityBuildPackage Hall(bool twin)
    {
        EntityBuildPackage package;
        Landmark(package, kLiftThere, At(kThereAt, kQuarterTurn));
        if (twin)
            Placed(package, kCrate, At(Vec3d{ 30.0, 0.0, 0.0 }));
        return package;
    }

    struct CarryWorld
    {
        CarryWorld()
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
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            Squad = *tags.RegisterTag("test.squad");
            Point = *tags.RegisterTag("test.point");
            EffectDefinition hex;
            hex.Duration = EffectDuration::Duration;
            hex.DurationSeconds = 60.0f;
            Hexed = world.GetResource<EffectRegistry>().Define("asset://effects/hexed.sdata", hex);
            world.SetResource(ZoneStateSerialization{ .Serializers = &Serializers, .Context = &Context });
            world.SetResource(ZonePlacement{ .PartitionAt = [this](const Vec3d&) -> std::optional<StoragePartitionId> {
                for (const ZoneId zone : { kHall, kRoom })
                    if (const RuntimeZoneRecord* record = Runtime.FindZone(zone);
                        record != nullptr && record->State == RuntimeZoneLoadState::Resident)
                        return record->Partition;
                return std::nullopt;
            } });
            world.AddResource<SensorOverlaps>();
            world.AddResource<ClaimBoard>();
            Hero = world.CreateEntity();
            world.AddComponent<PersistentIdComponent>(Hero, PersistentIdComponent{ kHeroId });
            world.AddComponent<LocalTransform>(Hero, LocalTransform{});
        }

        World& Entities() { return Runtime.Entities(); }
        ZoneStateStore& Store() { return Entities().GetResource<ZoneStateStore>(); }
        ZoneStateScope Scope(const LevelReference& level, ZoneId zone) { return Store().Scope(level, zone); }
        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }
        PersistentEntityId IdOf(EntityId entity) { return Entities().TryGet<PersistentIdComponent>(entity)->Id; }
        StoragePartitionId PartitionOf(ZoneId zone) { return Runtime.FindZone(zone)->Partition; }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(const LevelReference& level, ZoneId zone, const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, zone, package, Serializers, Context, kLogic, &error,
                                          Scope(level, zone)))
                << error.Message;
            FinishResidency();
        }

        void EnterCellar() { Import(kCellar, kRoom, Room()); }
        void EnterAttic(bool twin = false) { Import(kAttic, kHall, Hall(twin)); }

        void Leave(ZoneId zone)
        {
            ASSERT_TRUE(Runtime.RequestDetach(zone));
            FinishResidency();
        }

        void Equip()
        {
            World& world = Entities();
            const EntityId crate = Resolve(kCrate);
            world.AddComponent<ItemContainer>(crate, ItemContainer{});
            (void)Inventory(world, FixedSimTime{}).Give(crate, Arrows, 3);
            SettleHeldRecords(world);

            const EntityId guard = Resolve(kGuard);
            world.AddComponent<NavAgent>(guard, NavAgent{});
            FirstOrder = RequestNavigation(world, guard, NavDestination::AtEntity(Hero, true));
            ObservationMemory memory;
            memory.Count = 1;
            memory.Entries[0].Subject = Resolve(kHex);
            world.AddComponent<ObservationMemory>(guard, memory);
            world.AddComponent<DecisionAgent>(guard, DecisionAgent{});
            ASSERT_TRUE(world.GetResource<ClaimBoard>().Take(world, Squad, Point, LeaseOwner{ guard, 1 }, 1));
            world.TryGet<DecisionAgent>(guard)->ClaimOption = 0;
            SensorOverlaps& overlaps = world.GetResource<SensorOverlaps>();
            overlaps.Begin();
            (void)overlaps.Observe(SensorPair{ Resolve(kLiftHere), guard }, true);
            (void)overlaps.Commit(1);
            (void)ApplyEffect(world, guard, Hexed, FixedSimTime{}, LeaseOwner{ Resolve(kHex), 1 });
        }

        void TakeOut(std::vector<EntityId> entities)
        {
            Carry.TakeOut(Runtime, entities, "lift", [this](EntityId entity) {
                const RuntimeZoneRecord* record = Runtime.FindPartition(Entities().GetEntityPartition(entity));
                return record != nullptr ? record->StateScope : ZoneStateScope{};
            });
        }

        void Arrive()
        {
            Logger& log = Logging.GetLogger<CarryWorld>();
            Carry.Arrive(Runtime, nullptr, log);
        }

        std::vector<GainedZoneMember> Members(const LevelReference& level, ZoneId zone)
        {
            for (const DurableLevelState& state : Store().ExportDurable())
                if (state.Level == level)
                    for (const DurableZoneState& recorded : state.Zones)
                        if (recorded.Zone == zone && recorded.Lists != nullptr)
                            for (const ZoneStateList& list :
                                 DecodeZoneLists(*recorded.Lists).value_or(std::vector<ZoneStateList>{}))
                                if (list.Name == kGainedZoneList)
                                    return DecodeGainedList(list.Payload).value_or(std::vector<GainedZoneMember>{});
            return {};
        }

        std::size_t EffectsOn(EntityId target)
        {
            std::size_t count = 0;
            Entities().ForEachComponent<ActiveEffect>(
                [&](EntityId, const ActiveEffect& effect) { count += effect.Target == target ? 1 : 0; });
            return count;
        }

        LoggingProvider Logging;
        Lines& Log;
        SceneSerializationContext Context;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        LevelCarry Carry;
        ItemId Arrows;
        GameplayTagId Squad;
        GameplayTagId Point;
        EffectId Hexed;
        EntityId Hero;
        NavRequestId FirstOrder;
    };

    bool Near(const Vec3d& a, const Vec3d& b) { return (a - b).Magnitude() < 1e-4f; }
}

TEST(LevelCarry, ACrateArrivesAtTheSameOffsetFromTheLandmarkWithWhatItHoldsAndOutOfEverySimulationOnTheWay)
{
    CarryWorld world;
    world.EnterCellar();
    world.Equip();
    const EntityId crate = world.Resolve(kCrate);
    world.TakeOut({ crate });
    const StoragePartitionId holding = world.Runtime.HoldingPartition();
    EXPECT_EQ(world.Entities().GetEntityPartition(crate), holding);
    world.Leave(kRoom);
    ASSERT_TRUE(world.Entities().IsAlive(crate)) << "what is carried outlives the level it leaves";
    const FrameZoneView& view = world.Runtime.BuildFrameView();
    EXPECT_FALSE(view.Physics.Contains(holding)) << "a carried body would fall through the load";
    EXPECT_FALSE(view.Logic.Contains(holding));
    world.Runtime.EndFrameView();

    world.EnterAttic();
    world.Arrive();
    EXPECT_FALSE(world.Carry.Holds());
    EXPECT_EQ(world.Entities().GetEntityPartition(crate), world.PartitionOf(kHall));
    const Vec3d offset = kCrateAt - kLiftAt;
    const Vec3d expected = kThereAt + kQuarterTurn.RotateVector(offset);
    EXPECT_TRUE(Near(world.Entities().TryGet<LocalTransform>(crate)->Value.Position, expected));
    EXPECT_EQ(world.IdOf(crate), kCrate) << "it keeps its id";
    EXPECT_TRUE(world.Entities().HasComponent<KeptWithZone>(crate));
    const std::vector<EntityId> held = ItemsIn(world.Entities(), crate);
    ASSERT_EQ(held.size(), 1u) << "what it holds crossed with it";
    EXPECT_EQ(world.Entities().GetEntityPartition(held[0]), world.PartitionOf(kHall));

    world.Leave(kHall);
    const std::vector<GainedZoneMember> members = world.Members(kAttic, kHall);
    ASSERT_EQ(members.size(), 1u) << "a save after arrival holds it in the level it entered";
    EXPECT_EQ(GainedMemberId(members[0].Payload), kCrate);
    world.EnterAttic();
    ASSERT_TRUE(world.Resolve(kCrate).IsValid()) << "it is a member of the zone it arrived in";
    EXPECT_EQ(ItemsIn(world.Entities(), world.Resolve(kCrate)).size(), 1u);
}

TEST(LevelCarry, ACarriedEntityIsGoneFromItsOriginOnReturnAfterEnteringItAsAuthoredAndAfterARestWhenItResetsAtRest)
{
    CarryWorld world;
    world.EnterCellar();
    world.Entities().AddComponent<ZoneStateLifetime>(world.Resolve(kCrate),
                                                     ZoneStateLifetime{ ZoneStateForgetting::AtRestore });
    world.TakeOut({ world.Resolve(kCrate) });
    world.Leave(kRoom);
    world.EnterAttic();
    world.Arrive();
    const EntityId crate = world.Resolve(kCrate);
    world.Leave(kHall);
    ASSERT_TRUE(world.Entities().IsAlive(crate) == false);

    world.EnterCellar();
    EXPECT_FALSE(world.Resolve(kCrate).IsValid()) << "back as the game left it";
    world.Leave(kRoom);

    world.Store().ForgetLevel(kCellar);
    world.EnterCellar();
    EXPECT_FALSE(world.Resolve(kCrate).IsValid()) << "entered as authored";
    EXPECT_TRUE(world.Resolve(kShelf).IsValid());
    world.Leave(kRoom);

    world.Store().Install(world.Store().ExportDurable());
    world.EnterCellar();
    EXPECT_FALSE(world.Resolve(kCrate).IsValid()) << "after a rest, though it resets at rest";
    world.Leave(kRoom);

    world.Store().Clear();
    world.EnterCellar();
    EXPECT_TRUE(world.Resolve(kCrate).IsValid()) << "a new game has it back where content put it";
}

TEST(LevelCarry, AFollowerArrivesFollowingAgainAndForgetsTheLevelItLeft)
{
    CarryWorld world;
    world.EnterCellar();
    world.Equip();
    const EntityId guard = world.Resolve(kGuard);
    ASSERT_EQ(world.EffectsOn(guard), 1u);
    world.TakeOut({ guard });
    world.Leave(kRoom);
    TickEffects(world.Entities());
    world.EnterAttic();
    world.Arrive();

    const NavAgentRequest* order = world.Entities().TryGet<NavAgentRequest>(guard);
    ASSERT_NE(order, nullptr);
    EXPECT_NE(order->Id, world.FirstOrder) << "the order is given again, so its route is planned again";
    EXPECT_EQ(order->Kind, NavDestinationKind::Entity);
    EXPECT_EQ(order->Target, world.Hero);
    EXPECT_TRUE(order->Follow);
    EXPECT_EQ(world.Entities().TryGet<ObservationMemory>(guard)->Count, 0u) << "its memory of the old level";
    EXPECT_EQ(world.Entities().GetResource<ClaimBoard>().Count(world.Entities(), world.Squad, world.Point), 0u)
        << "its claim";
    EXPECT_EQ(world.Entities().TryGet<DecisionAgent>(guard)->ClaimOption, kNoDecisionOption);
    EXPECT_TRUE(std::ranges::none_of(world.Entities().GetResource<SensorOverlaps>().Pairs(),
                                     [&](const SensorPair& pair) { return pair.Activator == guard; }))
        << "its sensor pairs";
    EXPECT_EQ(world.EffectsOn(guard), 0u) << "an effect whose owner stayed behind";

    world.Leave(kHall);
    const std::vector<GainedZoneMember> members = world.Members(kAttic, kHall);
    ASSERT_EQ(members.size(), 1u);
    const std::string saved = JsonStringify(DecodeGainedMember(members[0].Payload)->Value);
    EXPECT_NE(saved.find("Nav Agent Request"), std::string::npos)
        << "a save after arrival keeps the order given again: " << saved;
}

TEST(LevelCarry, ItTakesARuntimeIdOnlyWhenTheLevelItEntersHoldsItsOwn)
{
    CarryWorld world;
    world.EnterCellar();
    world.Equip();
    const EntityId crate = world.Resolve(kCrate);
    world.TakeOut({ crate });
    world.Leave(kRoom);
    world.EnterAttic(true);
    world.Arrive();
    EXPECT_NE(world.IdOf(crate), kCrate);
    EXPECT_FALSE(IsAuthoredPersistentEntityId(world.IdOf(crate)));
    const EntityId twin = world.Resolve(kCrate);
    ASSERT_TRUE(twin.IsValid());
    EXPECT_NE(twin, crate) << "the level's own entity answers to its id";
    EXPECT_EQ(world.Resolve(world.IdOf(crate)), crate);
    EXPECT_EQ(ItemsIn(world.Entities(), crate).size(), 1u) << "what it holds names it by its new id";
}

TEST(LevelCarry, ACarryNamingNoLandmarkOrAChildedEntityThatIsNoSceneInstanceRootIsRefused)
{
    CarryWorld world;
    world.EnterCellar();
    world.Equip();
    const World& entities = world.Entities();
    const EntityId crate = world.Resolve(kCrate);
    EXPECT_EQ(RefuseCarrying(entities, std::vector{ crate }, ""), CarryRefusal::NoLandmark);
    EXPECT_EQ(RefuseCarrying(entities, std::vector{ crate }, "stairwell"), CarryRefusal::NoLandmark);
    EXPECT_EQ(RefuseCarrying(entities, std::vector{ crate }, "lift"), CarryRefusal::None);
    EXPECT_EQ(RefuseCarrying(entities, std::vector{ world.Resolve(kShelf) }, "lift"), CarryRefusal::NotCarriable)
        << "its child would be lost at the first save";
    EXPECT_EQ(RefuseCarrying(entities, std::vector{ world.Resolve(kBook) }, "lift"), CarryRefusal::NotCarriable);
    EXPECT_EQ(RefuseCarrying(entities, ItemsIn(entities, crate), "lift"), CarryRefusal::NotCarriable);
}

TEST(LevelCarry, WhatIsHeldWhenTheLevelItEntersHasNoSuchLandmarkIsLostAndReported)
{
    CarryWorld world;
    world.EnterCellar();
    const EntityId crate = world.Resolve(kCrate);
    world.TakeOut({ crate });
    world.Leave(kRoom);
    world.Import(kAttic, kHall, EntityBuildPackage{});
    world.Arrive();
    EXPECT_FALSE(world.Entities().IsAlive(crate));
    EXPECT_FALSE(world.Carry.Holds());
    ASSERT_EQ(world.Log.Errors.size(), 1u);
    EXPECT_NE(world.Log.Errors[0].find("lift"), std::string::npos);
}

namespace
{
    EntityId PlaceRuntime(World& world, StoragePartitionId partition, const Transform3f& pose,
                          EntityId parent = EntityId{})
    {
        const EntityId entity = world.CreateEntity(partition);
        world.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(entity, WorldTransform{ parent.IsValid()
                                                                        ? world.TryGet<WorldTransform>(parent)->Value * pose
                                                                        : pose });
        if (parent.IsValid())
            world.AddComponent<Parent>(entity, Parent{ parent });
        world.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{ .Previous = pose,
                                                                                .Current = pose,
                                                                                .State = HistoryState::Steady });
        return entity;
    }
}

TEST(LevelCarry, ACarriedRootAndItsChildArriveTurnedWithTheLandmarkAndBothHistoriesSnap)
{
    CarryWorld world;
    world.EnterCellar();
    World& entities = world.Entities();
    const EntityId cart = PlaceRuntime(entities, world.PartitionOf(kRoom), At(kCrateAt));
    const EntityId load = PlaceRuntime(entities, world.PartitionOf(kRoom), At(Vec3d(0.0, 1.0, 0.0)), cart);

    world.TakeOut({ cart });
    world.Leave(kRoom);
    world.EnterAttic();
    world.Arrive();

    const Vec3d arrived = kThereAt + kQuarterTurn.RotateVector(kCrateAt - kLiftAt);
    EXPECT_TRUE(Near(entities.TryGet<LocalTransform>(cart)->Value.Position, arrived));
    EXPECT_TRUE(entities.TryGet<LocalTransform>(cart)->Value.Rotation.NearlyEquals(kQuarterTurn, 1e-5f));
    EXPECT_TRUE(Near(entities.TryGet<LocalTransform>(load)->Value.Position, Vec3d(0.0, 1.0, 0.0)));
    EXPECT_TRUE(Near(entities.TryGet<WorldTransform>(load)->Value.Position,
                     arrived + kQuarterTurn.RotateVector(Vec3d(0.0, 1.0, 0.0))));
    EXPECT_EQ(entities.GetEntityPartition(load), world.PartitionOf(kHall));
    for (const EntityId member : { cart, load })
        EXPECT_EQ(entities.TryGet<WorldTransformHistory>(member)->State, HistoryState::Snapping)
            << "out of every simulation for the load, so no motion is carried across";
    StoragePartitionSet hall;
    hall.Add(world.PartitionOf(kHall));
    CaptureWorldTransformHistory(entities, hall, false);
    for (const EntityId member : { cart, load })
        EXPECT_TRUE(Near(ResolvePresentationPose(*entities.TryGet<WorldTransformHistory>(member), 0.0).Position,
                         entities.TryGet<WorldTransform>(member)->Value.Position))
            << "drawn where it arrived, never at the level it left";
}

TEST(LevelCarry, ACarriedEntitysVelocitiesAndAimTurnWithTheLandmark)
{
    CarryWorld world;
    world.EnterCellar();
    World& entities = world.Entities();
    const EntityId crate = world.Resolve(kCrate);
    const Vec3d east(1.0f, 0.0f, 0.0f);
    entities.AddComponent<KinematicState>(crate, KinematicState{ .Velocity = east });
    RigidBody body;
    body.LinearVelocity = east;
    body.AngularVelocity = east;
    entities.AddComponent<RigidBody>(crate, body);
    entities.AddComponent<LookOrientation>(crate, LookOrientation{ .Yaw = 0.3f });

    world.TakeOut({ crate });
    world.Leave(kRoom);
    world.EnterAttic();
    world.Arrive();

    const Vec3d turned = kQuarterTurn.RotateVector(east);
    EXPECT_TRUE(Near(entities.TryGet<KinematicState>(crate)->Velocity, turned));
    EXPECT_TRUE(Near(entities.TryGet<RigidBody>(crate)->LinearVelocity, turned));
    EXPECT_TRUE(Near(entities.TryGet<RigidBody>(crate)->AngularVelocity, turned));
    EXPECT_NEAR(entities.TryGet<LookOrientation>(crate)->Yaw, 0.3f + std::numbers::pi_v<float> * 0.5f, 1e-4f)
        << "an aim turns with the landmark, as its rotation always did";
}

TEST(LevelCarry, WhatARelocationRefusesIsLostAndTheLogNamesWhy)
{
    CarryWorld world;
    world.EnterCellar();
    World& entities = world.Entities();
    const EntityId crate = world.Resolve(kCrate);
    entities.AddComponent<AimFacing>(crate);
    RefuseRelocation<AimFacing>(entities);

    world.TakeOut({ crate });
    world.Leave(kRoom);
    world.EnterAttic();
    world.Arrive();

    EXPECT_FALSE(entities.IsAlive(crate));
    EXPECT_FALSE(world.Carry.Holds());
    ASSERT_EQ(world.Log.Errors.size(), 1u);
    EXPECT_NE(world.Log.Errors[0].find("sencha.aim_facing"), std::string::npos) << world.Log.Errors[0];
}
