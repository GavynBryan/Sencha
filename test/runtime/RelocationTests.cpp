#include <abilities/AbilityKit.h>
#include <attach/Attachment.h>
#include <controller/LookOrientation.h>
#include <ecs/WorldComponentSchema.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <line/ContactLine.h>
#include <logic/decision/DecisionAgent.h>
#include <math/geometry/3d/RigidMapping.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementRegistration.h>
#include <movement/components/HeldMotion.h>
#include <navigation/NavAgent.h>
#include <navigation/NavAgentRequests.h>
#include <perception/ObservationMemory.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/RigidBody.h>
#include <projectile/ProjectileState.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/held/HeldRecords.h>
#include <world/relocation/Relocation.h>
#include <world/relocation/RelocationRules.h>
#include <world/relocation/RelocationSerial.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

// A component a game might add, for the rules a later component brings with it.
struct TestAnchor
{
    Vec3d Point = Vec3d::Zero();
};
SENCHA_DECLARE_COMPONENT_TYPE(TestAnchor, "test.relocation.anchor");

namespace
{
    constexpr StoragePartitionId kHere{ 5 };
    constexpr StoragePartitionId kThere{ 6 };
    constexpr float kQuarter = std::numbers::pi_v<float> * 0.5f;
    const Quatf kQuarterTurn = Quatf::FromAxisAngle(Vec3d::Up(), kQuarter);
    const Transform3f kMapping{ Vec3d(100.0f, 0.0f, -20.0f), kQuarterTurn, Vec3d::One() };

    bool Near(const Vec3d& a, const Vec3d& b) { return (a - b).Magnitude() < 1e-4f; }

    void MapAnchor(TestAnchor& anchor, const RelocationFrame& frame)
    {
        anchor.Point = frame.MapPoint(anchor.Point);
    }

    WorldComponentSchema EngineSchema()
    {
        WorldComponentSchema schema;
        ComponentRegistrar registrar(&schema, nullptr, nullptr);
        RegisterEngineComponents(registrar);
        registrar.Add<TestAnchor>();
        schema.Seal();
        return schema;
    }

    struct RelocationWorld
    {
        RelocationWorld()
            : Schema(EngineSchema())
            , Runtime(Schema)
        {
            InstallMovementVocabulary(Entities());
        }

        World& Entities() { return Runtime.Entities(); }

        EntityId Spawn(const Vec3d& position, StoragePartitionId partition = kHere)
        {
            World& world = Entities();
            const Transform3f pose{ position, Quatf::Identity(), Vec3d::One() };
            const EntityId entity = world.CreateEntity(partition);
            world.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
            world.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
            return entity;
        }

        EntityId SpawnChild(EntityId parent, const Vec3d& offset)
        {
            World& world = Entities();
            const Transform3f local{ offset, Quatf::Identity(), Vec3d::One() };
            const Transform3f placed = world.TryGet<WorldTransform>(parent)->Value * local;
            const EntityId child = world.CreateEntity(world.GetEntityPartition(parent));
            world.AddComponent<LocalTransform>(child, LocalTransform{ local });
            world.AddComponent<WorldTransform>(child, WorldTransform{ placed });
            world.AddComponent<Parent>(child, Parent{ parent });
            return child;
        }

        RelocationResult Relocate(EntityId root, const Transform3f& mapping = kMapping,
                                  std::optional<StoragePartitionId> partition = kThere)
        {
            return RelocateEntity(Entities(), root, RigidRelocation{ .Mapping = mapping, .Partition = partition });
        }

        template <typename T>
        const T& Read(EntityId entity)
        {
            return *std::as_const(Entities()).TryGet<T>(entity);
        }

        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
    };
}

TEST(Relocation, EveryEngineEntryRelocatesTheStateItOwns)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId mover = fx.Spawn(Vec3d(1.0f, 0.0f, 2.0f));
    const Vec3d east(1.0f, 0.0f, 0.0f);
    const Vec3d turned = kQuarterTurn.RotateVector(east);
    world.AddComponent<KinematicState>(mover, KinematicState{ .Velocity = east });
    world.AddComponent<MovementIntent>(mover, MovementIntent{ .WishDir = east });
    MotionBursts bursts;
    bursts.Slots[0].Owner = LeaseOwner{ mover, 1 };
    bursts.Slots[0].Velocity = east;
    world.AddComponent<MotionBursts>(mover, bursts);
    world.AddComponent<MotionImpulse>(mover, MotionImpulse{ .DeltaVelocity = east });
    world.AddComponent<MotionAxisOverride>(mover, MotionAxisOverride{ .PlanarVelocity = east });
    world.AddComponent<LocomotionOutput>(mover, LocomotionOutput{ .Velocity = east });
    world.AddComponent<MotionRequest>(mover, MotionRequest{ .Velocity = east });
    SupportState support;
    support.Normal = east;
    support.Kind = SupportKind::Stable;
    world.AddComponent<SupportState>(mover, support);
    Immersion immersion;
    immersion.Volume = mover;
    immersion.Fraction = 0.5f;
    world.AddComponent<Immersion>(mover, immersion);
    TetherSession tether;
    tether.Owner = LeaseOwner{ mover, 2 };
    tether.Point = east;
    tether.Length = 3.0f;
    world.AddComponent<TetherSession>(mover, tether);
    RigidBody body;
    body.LinearVelocity = east;
    body.AngularVelocity = east;
    world.AddComponent<RigidBody>(mover, body);
    world.AddComponent<ProjectileMotion>(mover, ProjectileMotion{ .Velocity = east });
    world.AddComponent<LookOrientation>(mover, LookOrientation{ .Yaw = 0.25f });
    ObservationMemory memory;
    memory.Count = 1;
    memory.Entries[0].Position = east;
    world.AddComponent<ObservationMemory>(mover, memory);
    DecisionAgent agent;
    agent.Option = 3;
    agent.HasDestination = true;
    agent.Destination = east;
    world.AddComponent<DecisionAgent>(mover, agent);
    world.AddComponent<NavAgentRequest>(mover, NavAgentRequest{});
    NavDestination destination;
    destination.Point = east;
    const NavRequestId first = RequestNavigation(world, mover, destination);

    const RelocationResult result = fx.Relocate(mover);

    ASSERT_EQ(result.Outcome, RelocationOutcome::Relocated);
    EXPECT_FALSE(result.KeptUprightState);
    EXPECT_TRUE(Near(fx.Read<LocalTransform>(mover).Value.Position, kMapping.TransformPoint(Vec3d(1.0f, 0.0f, 2.0f))));
    EXPECT_TRUE(fx.Read<LocalTransform>(mover).Value.Rotation.NearlyEquals(kQuarterTurn, 1e-5f));
    EXPECT_TRUE(Near(fx.Read<WorldTransform>(mover).Value.Position, kMapping.TransformPoint(Vec3d(1.0f, 0.0f, 2.0f))));
    EXPECT_TRUE(Near(fx.Read<KinematicState>(mover).Velocity, turned));
    EXPECT_TRUE(Near(fx.Read<MovementIntent>(mover).WishDir, turned));
    EXPECT_TRUE(Near(fx.Read<MotionBursts>(mover).Slots[0].Velocity, turned));
    EXPECT_TRUE(Near(fx.Read<MotionImpulse>(mover).DeltaVelocity, turned));
    EXPECT_TRUE(Near(fx.Read<MotionAxisOverride>(mover).PlanarVelocity, turned));
    EXPECT_TRUE(Near(fx.Read<LocomotionOutput>(mover).Velocity, turned));
    EXPECT_TRUE(Near(fx.Read<LocomotionOutput>(mover).UpAxis, Vec3d::Up()));
    EXPECT_TRUE(Near(fx.Read<MotionRequest>(mover).Velocity, turned));
    EXPECT_EQ(fx.Read<SupportState>(mover).Kind, SupportKind::None) << "support is found again on the far side";
    EXPECT_FALSE(fx.Read<Immersion>(mover).Volume.IsValid());
    EXPECT_TRUE(fx.Read<TetherSession>(mover).Owner.IsSet()) << "a tether is not cut by a relocation";
    EXPECT_TRUE(Near(fx.Read<TetherSession>(mover).Point, kMapping.TransformPoint(east)))
        << "a tether to a point keeps its point where the mapping put it";
    EXPECT_TRUE(Near(fx.Read<RigidBody>(mover).LinearVelocity, turned));
    EXPECT_TRUE(Near(fx.Read<RigidBody>(mover).AngularVelocity, turned));
    EXPECT_TRUE(Near(fx.Read<ProjectileMotion>(mover).Velocity, turned));
    EXPECT_NEAR(fx.Read<LookOrientation>(mover).Yaw, 0.25f + kQuarter, 1e-5f);
    EXPECT_EQ(fx.Read<ObservationMemory>(mover).Count, 0u);
    EXPECT_FALSE(fx.Read<DecisionAgent>(mover).HasDestination);
    EXPECT_EQ(fx.Read<DecisionAgent>(mover).Option, 3u) << "only its places are forgotten";
    const NavAgentRequest& order = fx.Read<NavAgentRequest>(mover);
    EXPECT_NE(order.Id, first) << "the order is given again";
    EXPECT_TRUE(Near(order.Point, kMapping.TransformPoint(east)));
    EXPECT_EQ(world.GetEntityPartition(mover), kThere);
}

TEST(Relocation, TheTurnedYawFacesTheWayTheMappingTurnsTheBody)
{
    RelocationWorld fx;
    const EntityId turret = fx.Spawn(Vec3d::Zero());
    fx.Entities().AddComponent<LookOrientation>(turret, LookOrientation{ .Yaw = 0.4f });
    const Quatf turn = Quatf::FromAxisAngle(Vec3d::Up(), -2.5f);

    ASSERT_EQ(fx.Relocate(turret, Transform3f{ Vec3d::Zero(), turn, Vec3d::One() }).Outcome,
              RelocationOutcome::Relocated);

    const Vec3d expected = (turn * Quatf::FromAxisAngle(Vec3d::Up(), 0.4f)).RotateVector(Vec3d::Forward());
    const Vec3d facing =
        Quatf::FromAxisAngle(Vec3d::Up(), fx.Read<LookOrientation>(turret).Yaw).RotateVector(Vec3d::Forward());
    EXPECT_TRUE(Near(facing, expected));
}

TEST(Relocation, AMappingThatTiltsUpKeepsTheYawAndSaysSo)
{
    RelocationWorld fx;
    const EntityId turret = fx.Spawn(Vec3d::Zero());
    fx.Entities().AddComponent<LookOrientation>(turret, LookOrientation{ .Yaw = 0.4f });
    fx.Entities().AddComponent<KinematicState>(turret, KinematicState{ .Velocity = Vec3d::Up() });
    const Quatf tilt = Quatf::FromAxisAngle(Vec3d::Right(), kQuarter);

    const RelocationResult result = fx.Relocate(turret, Transform3f{ Vec3d::Zero(), tilt, Vec3d::One() });

    ASSERT_EQ(result.Outcome, RelocationOutcome::Relocated);
    EXPECT_TRUE(result.KeptUprightState);
    EXPECT_FLOAT_EQ(fx.Read<LookOrientation>(turret).Yaw, 0.4f) << "a yaw about world up has no tilted meaning";
    EXPECT_TRUE(Near(fx.Read<KinematicState>(turret).Velocity, tilt.RotateVector(Vec3d::Up())))
        << "what is not held about world up still turns";
}

TEST(Relocation, ATetherToAnEntityKeepsNamingItWhetherOrNotItMovedToo)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId hook = fx.Spawn(Vec3d(5.0f, 0.0f, 0.0f));
    const EntityId hauler = fx.Spawn(Vec3d::Zero());
    const EntityId hauled = fx.SpawnChild(hauler, Vec3d(1.0f, 0.0f, 0.0f));
    TetherSession tether;
    tether.Owner = LeaseOwner{ hook, 1 };
    tether.Point = Vec3d(9.0f, 9.0f, 9.0f);
    tether.Length = 2.0f;
    tether.Anchor = hauler;
    world.AddComponent<TetherSession>(hauled, tether);
    tether.Anchor = hook;
    world.AddComponent<TetherSession>(hauler, tether);

    ASSERT_EQ(fx.Relocate(hauler).Outcome, RelocationOutcome::Relocated);

    EXPECT_EQ(fx.Read<TetherSession>(hauled).Anchor, hauler) << "an anchor carried along stays valid";
    EXPECT_EQ(fx.Read<TetherSession>(hauler).Anchor, hook) << "an anchor left behind is the caller's question";
    EXPECT_TRUE(fx.Read<TetherSession>(hauler).Owner.IsSet());
    EXPECT_TRUE(Near(fx.Read<TetherSession>(hauled).Point, Vec3d(9.0f, 9.0f, 9.0f)))
        << "the point of a tether to an entity means nothing and is left alone";
}

TEST(Relocation, AClingToASurfaceLeftBehindLeavesTheModeThroughItsOwnRequest)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId wall = fx.Spawn(Vec3d(0.0f, 0.0f, -1.0f));
    const EntityId climber = fx.Spawn(Vec3d::Zero());
    world.AddComponent<ModeTransitionRequest>(climber, ModeTransitionRequest{});
    ClingSession cling;
    cling.Surface = wall;
    cling.Anchor = Vec3d(1.0f, 2.0f, 3.0f);
    world.AddComponent<ClingSession>(climber, cling);

    ASSERT_EQ(fx.Relocate(climber).Outcome, RelocationOutcome::Relocated);

    const ModeTransitionRequest& request = fx.Read<ModeTransitionRequest>(climber);
    EXPECT_TRUE(request.Pending);
    EXPECT_EQ(request.Class, ModeRequestClass::Forced);
    EXPECT_EQ(request.Target, world.GetResource<LocomotionModeRegistry>().FreeMode());
}

TEST(Relocation, AClingToASurfaceThatMovedTooIsCarriedAndKept)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId raft = fx.Spawn(Vec3d::Zero());
    const EntityId climber = fx.SpawnChild(raft, Vec3d(0.0f, 1.0f, 0.0f));
    world.AddComponent<ModeTransitionRequest>(climber, ModeTransitionRequest{});
    ClingSession cling;
    cling.Surface = raft;
    cling.Anchor = Vec3d(1.0f, 2.0f, 3.0f);
    cling.SurfaceNormal = Vec3d(1.0f, 0.0f, 0.0f);
    world.AddComponent<ClingSession>(climber, cling);

    ASSERT_EQ(fx.Relocate(raft).Outcome, RelocationOutcome::Relocated);

    EXPECT_FALSE(fx.Read<ModeTransitionRequest>(climber).Pending);
    EXPECT_TRUE(Near(fx.Read<ClingSession>(climber).Anchor, kMapping.TransformPoint(Vec3d(1.0f, 2.0f, 3.0f))));
    EXPECT_TRUE(Near(fx.Read<ClingSession>(climber).SurfaceNormal, kQuarterTurn.RotateVector(Vec3d(1.0f, 0.0f, 0.0f))));
}

TEST(Relocation, ALineOutFromARelocatedInstigatorDrawsHome)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId caster = fx.Spawn(Vec3d::Zero());
    const EntityId elsewhere = fx.Spawn(Vec3d::Zero());
    const EntityId line = fx.Spawn(Vec3d(10.0f, 0.0f, 0.0f));
    const EntityId other = fx.Spawn(Vec3d(10.0f, 0.0f, 0.0f));
    ContactLineState state;
    state.Instigator = caster;
    state.Stage = ContactLineStage::Anchored;
    state.Kept = true;
    world.AddComponent<ContactLineState>(line, state);
    state.Instigator = elsewhere;
    world.AddComponent<ContactLineState>(other, state);

    ASSERT_EQ(fx.Relocate(caster).Outcome, RelocationOutcome::Relocated) << "a line does not hold a relocation back";

    EXPECT_EQ(fx.Read<ContactLineState>(line).Stage, ContactLineStage::Retracting);
    EXPECT_EQ(fx.Read<ContactLineState>(other).Stage, ContactLineStage::Anchored);
}

TEST(Relocation, AComponentWhoseRuleRefusesStopsTheWholeGroupByNameAndChangesNothing)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId root = fx.Spawn(Vec3d(1.0f, 0.0f, 0.0f));
    const EntityId marker = fx.SpawnChild(root, Vec3d(0.0f, 1.0f, 0.0f));
    world.AddComponent<TestAnchor>(marker, TestAnchor{});
    world.AddComponent<KinematicState>(root, KinematicState{ .Velocity = Vec3d(1.0f, 0.0f, 0.0f) });
    RefuseRelocation<TestAnchor>(world);

    const RelocationResult result = fx.Relocate(root);

    EXPECT_EQ(result.Outcome, RelocationOutcome::Refused);
    EXPECT_EQ(result.Component, "test.relocation.anchor");
    EXPECT_TRUE(Near(fx.Read<LocalTransform>(root).Value.Position, Vec3d(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(Near(fx.Read<KinematicState>(root).Velocity, Vec3d(1.0f, 0.0f, 0.0f)));
    EXPECT_EQ(world.GetEntityPartition(root), kHere);
    EXPECT_EQ(RelocationSerialOf(world, root), 0u);
}

TEST(Relocation, ALaterComponentAddsItsOwnRuleOrRefusal)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId marker = fx.Spawn(Vec3d::Zero());
    world.AddComponent<TestAnchor>(marker, TestAnchor{ .Point = Vec3d(0.0f, 0.0f, -1.0f) });

    RefuseRelocation<TestAnchor>(world);
    EXPECT_EQ(fx.Relocate(marker).Component, "test.relocation.anchor");

    SetRelocationRule<TestAnchor>(world, RelocationRuleKind::Maps, &RelocateValue<TestAnchor, &MapAnchor>);
    ASSERT_EQ(fx.Relocate(marker).Outcome, RelocationOutcome::Relocated);
    EXPECT_TRUE(Near(fx.Read<TestAnchor>(marker).Point, kMapping.TransformPoint(Vec3d(0.0f, 0.0f, -1.0f))));
}

TEST(Relocation, WhatTheOperationCannotMapIsRefusedBeforeAnythingMoves)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId root = fx.Spawn(Vec3d::Zero());
    const EntityId child = fx.SpawnChild(root, Vec3d(0.0f, 1.0f, 0.0f));

    EXPECT_EQ(fx.Relocate(child).Outcome, RelocationOutcome::Parented);
    EXPECT_EQ(fx.Relocate(root, Transform3f{ Vec3d::Zero(), Quatf::Identity(), Vec3d(2.0f, 2.0f, 2.0f) }).Outcome,
              RelocationOutcome::NotRigid);
    world.DestroyEntity(child);
    EXPECT_EQ(fx.Relocate(child).Outcome, RelocationOutcome::Gone);
    EXPECT_EQ(world.GetEntityPartition(root), kHere);
    EXPECT_EQ(RelocationSerialOf(world, root), 0u);
}

TEST(Relocation, TheSerialAdvancesOncePerRelocationAndTheFrameRecordsEachMapping)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId root = fx.Spawn(Vec3d::Zero());
    const EntityId child = fx.SpawnChild(root, Vec3d(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(RelocationSerialOf(world, root), 0u);

    EXPECT_EQ(fx.Relocate(root).Serial, 1u);
    const Transform3f back = InverseRigidMapping(kMapping);
    EXPECT_EQ(fx.Relocate(root, back, kHere).Serial, 2u);

    EXPECT_EQ(RelocationSerialOf(world, root), 2u);
    EXPECT_EQ(RelocationSerialOf(world, child), 2u) << "a child's world pose jumped with its root's";
    const std::span<const FrameRelocation> record = world.GetResource<FrameRelocations>().All();
    ASSERT_EQ(record.size(), 4u);
    EXPECT_EQ(record[0].Entity, root);
    EXPECT_EQ(record[0].Serial, 1u);
    EXPECT_TRUE(record[0].Mapping.NearlyEquals(kMapping, 1e-5f));
    EXPECT_EQ(record[1].Entity, child);
    EXPECT_EQ(record[2].Serial, 2u);
    EXPECT_TRUE(record[2].Mapping.NearlyEquals(back, 1e-5f));

    ClearFrameRelocations(world);
    EXPECT_TRUE(world.GetResource<FrameRelocations>().All().empty());
}

TEST(Relocation, ADescendantKeepsItsLocalPoseAndFollowsInWorldSpaceAndPartition)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId root = fx.Spawn(Vec3d(2.0f, 0.0f, 0.0f));
    const EntityId child = fx.SpawnChild(root, Vec3d(0.0f, 1.0f, 0.0f));

    ASSERT_EQ(fx.Relocate(root).Outcome, RelocationOutcome::Relocated);

    EXPECT_TRUE(Near(fx.Read<LocalTransform>(child).Value.Position, Vec3d(0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE(Near(fx.Read<WorldTransform>(child).Value.Position, kMapping.TransformPoint(Vec3d(2.0f, 1.0f, 0.0f))));
    EXPECT_EQ(world.GetEntityPartition(child), kThere);
}

TEST(Relocation, ThePartitionMoveCarriesAttachmentsAndHeldRecords)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    InstallAttachments(world, nullptr);
    InstallInventoryVocabulary(world);
    const ItemId arrows =
        world.GetResource<ItemRegistry>().Define("asset://items/arrow.sdata", ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
    const EntityId crate = fx.Spawn(Vec3d::Zero());
    world.AddComponent<ItemContainer>(crate, ItemContainer{});
    (void)Inventory(world, FixedSimTime{}).Give(crate, arrows, 3);
    SettleHeldRecords(world);
    const EntityId badge = fx.Spawn(Vec3d(9.0f, 9.0f, 9.0f));
    ASSERT_EQ(AttachToOrigin(world, crate, badge), AttachResult::Attached);
    const std::vector<EntityId> held = ItemsIn(world, crate);
    ASSERT_EQ(held.size(), 1u);

    ASSERT_EQ(fx.Relocate(crate).Outcome, RelocationOutcome::Relocated);

    EXPECT_EQ(world.GetEntityPartition(badge), kThere);
    EXPECT_EQ(world.GetEntityPartition(held[0]), kThere);
}

TEST(Relocation, TheSensorPairsOfWhatMovedAreForgotten)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    SensorOverlaps& overlaps = world.AddResource<SensorOverlaps>();
    const EntityId sensor = fx.Spawn(Vec3d::Zero());
    const EntityId walker = fx.Spawn(Vec3d::Zero());
    const EntityId bystander = fx.Spawn(Vec3d::Zero());
    overlaps.Begin();
    (void)overlaps.Observe(SensorPair{ sensor, walker }, true);
    (void)overlaps.Observe(SensorPair{ sensor, bystander }, true);
    (void)overlaps.Commit(1);

    ASSERT_EQ(fx.Relocate(walker).Outcome, RelocationOutcome::Relocated);

    const auto pairs = overlaps.Pairs();
    EXPECT_TRUE(std::ranges::none_of(pairs, [&](const SensorPair& pair) { return pair.Activator == walker; }));
    EXPECT_TRUE(std::ranges::any_of(pairs, [&](const SensorPair& pair) { return pair.Activator == bystander; }));
}

TEST(Relocation, WithNoPartitionEveryMemberStaysWhereItIs)
{
    RelocationWorld fx;
    const EntityId root = fx.Spawn(Vec3d::Zero());

    ASSERT_EQ(fx.Relocate(root, kMapping, std::nullopt).Outcome, RelocationOutcome::Relocated);

    EXPECT_EQ(fx.Entities().GetEntityPartition(root), kHere);
}

TEST(Relocation, WhatIsDrawnBetweenTicksIsRelocatedInPlaceWithoutAStructuralChange)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId walker = fx.Spawn(Vec3d::Zero());
    world.AddComponent<WorldTransformHistory>(walker, WorldTransformHistory{});
    ASSERT_TRUE(world.HasComponent<RelocationSerial>(walker)) << "the history brings its serial";
    const std::uint64_t before = world.StructuralVersion();

    ASSERT_EQ(fx.Relocate(walker, kMapping, std::nullopt).Serial, 1u);

    EXPECT_EQ(world.StructuralVersion(), before);
}

TEST(Relocation, TheFrameRecordKeepsItsHighWaterAcrossAClear)
{
    RelocationWorld fx;
    World& world = fx.Entities();
    const EntityId root = fx.Spawn(Vec3d::Zero());
    (void)fx.SpawnChild(root, Vec3d(0.0f, 1.0f, 0.0f));

    (void)fx.Relocate(root);
    ClearFrameRelocations(world);
    (void)fx.Relocate(root, InverseRigidMapping(kMapping), kHere);

    EXPECT_EQ(world.GetResource<FrameRelocations>().All().size(), 2u);
    EXPECT_EQ(world.GetResource<FrameRelocations>().HighWater(), 2u) << "one relocation of a root and a child";
}
