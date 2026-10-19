#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlapSystem.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/CharacterController.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <numbers>

namespace
{
constexpr EntityId kSensor{ 10, 2 };
constexpr EntityId kActor{ 20, 3 };
constexpr SensorPair kPair{ kSensor, kActor };

PhysicsBodyId AddSensor(PhysicsWorld& world, Vec3d position = Vec3d::Zero())
{
    BodyDesc body;
    body.Shape = CollisionShape::MakeBox(Vec3d(0.5f, 2.0f, 2.0f));
    body.Position = position;
    body.IsTrigger = true;
    body.Layer = CollisionLayer::Trigger;
    body.UserData = PackEntity(kSensor);
    return world.AddBody(body);
}

void ExpectTransitions(const SensorOverlaps& overlaps, std::initializer_list<SensorTransition> transitions)
{
    ASSERT_EQ(overlaps.Occurrences().size(), transitions.size());
    std::size_t i = 0;
    for (auto transition : transitions)
    {
        EXPECT_EQ(overlaps.Occurrences()[i].Transition, transition);
        EXPECT_EQ(overlaps.Occurrences()[i].Pair, kPair);
        ++i;
    }
}
}

TEST(SensorOverlaps, EnterStayExitAndReentry)
{
    SensorOverlaps overlaps;
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    EXPECT_EQ(overlaps.Occurrences()[0].Tick, 1u);
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Stay });
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Commit(3));
    ExpectTransitions(overlaps, { SensorTransition::Exit });
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(4));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
}

TEST(SensorOverlaps, CrossingPublishesEnterThenExitWithoutOccupancy)
{
    SensorOverlaps overlaps;
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, false));
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
    EXPECT_TRUE(overlaps.Pairs().empty());
}

TEST(SensorOverlaps, EndOccupancyWinsOverSweepAndDepartureDoesNotReenter)
{
    SensorOverlaps overlaps;
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, false));
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, false));
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Exit });
}

TEST(SensorOverlaps, SortedIndependentOfObservationOrderAndGenerationallySafe)
{
    const std::array pairs{ kPair, SensorPair{ kSensor, { kActor.Index, kActor.Generation + 1 } },
                           SensorPair{ { 2, 1 }, kActor } };
    SensorOverlaps a;
    SensorOverlaps b;
    a.Begin();
    b.Begin();
    for (auto pair : pairs)
        ASSERT_TRUE(a.Observe(pair, true));
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
        ASSERT_TRUE(b.Observe(*it, true));
    ASSERT_TRUE(a.Commit(1));
    ASSERT_TRUE(b.Commit(1));
    ASSERT_EQ(a.Pairs().size(), 3u);
    EXPECT_TRUE(std::equal(a.Pairs().begin(), a.Pairs().end(), b.Pairs().begin(), b.Pairs().end()));
    EXPECT_TRUE(std::is_sorted(a.Pairs().begin(), a.Pairs().end()));
}

TEST(SensorOverlaps, OverflowRejectsWholeSampleWithoutFalseExits)
{
    SensorOverlaps overlaps(1);
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(1));
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe({ kSensor, { 30, 1 } }, true));
    EXPECT_FALSE(overlaps.Observe(kPair, true));
    EXPECT_FALSE(overlaps.Commit(2));
    EXPECT_EQ(overlaps.OverflowCount(), 1u);
    EXPECT_TRUE(overlaps.Occurrences().empty());
    ASSERT_EQ(overlaps.Pairs().size(), 1u);
    EXPECT_EQ(overlaps.Pairs()[0], kPair);
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(3));
    ExpectTransitions(overlaps, { SensorTransition::Stay });
}

TEST(SensorOverlaps, TeardownForgetsWithoutGameplayExit)
{
    SensorOverlaps overlaps;
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe(kPair, true));
    ASSERT_TRUE(overlaps.Commit(1));
    overlaps.ForgetIf([](SensorPair pair) { return pair.Sensor == kSensor; });
    EXPECT_TRUE(overlaps.Occurrences().empty());
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Commit(2));
    EXPECT_TRUE(overlaps.Occurrences().empty());
}

TEST(SensorPhysics, CharacterVirtualEntersStaysAndLeavesWithoutSensorBlocking)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(-2, 0, 0));
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(0));
    EXPECT_TRUE(overlaps.Occurrences().empty());
    CharacterMoveRequest move;
    move.DeltaSeconds = 0.1f;
    move.Velocity = Vec3d(20, 0, 0);
    (void)mover.Move(move);
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Stay });
    (void)mover.Move(move);
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(3));
    ExpectTransitions(overlaps, { SensorTransition::Exit });
    EXPECT_GT(mover.GetPosition().X, 1.5f);
}

TEST(SensorPhysics, FastCharacterCrossingIsObserved)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(-5, 0, 0));
    CharacterMoveRequest move;
    move.DeltaSeconds = 0.1f;
    move.Velocity = Vec3d(100, 0, 0);
    (void)mover.Move(move);
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
}

TEST(SensorPhysics, TeleportSamplesDestinationWithoutSweepingSkippedSpace)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(-5, 0, 0));
    mover.SetPosition(Vec3d(5, 0, 0));
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    EXPECT_TRUE(overlaps.Occurrences().empty());
    mover.SetPosition(Vec3d::Zero());
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
}

TEST(SensorPhysics, RigidBodyOverlapAndFastCrossingUseRetainedShape)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    BodyDesc desc;
    desc.Motion = BodyMotion::Kinematic;
    desc.Shape = CollisionShape::MakeSphere(0.25f);
    desc.UserData = PackEntity(kActor);
    const auto body = world.AddBody(desc);
    ASSERT_TRUE(body.IsValid());
    SensorOverlaps overlaps;
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    world.SetBodyTransform(body, Vec3d(5, 0, 0), Quatf::Identity());
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Exit });
    world.SetBodyTransform(body, Vec3d(-5, 0, 0), Quatf::Identity());
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    ASSERT_TRUE(overlaps.Commit(3));
    ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
}

TEST(SensorPhysics, OrientedSensorUsesItsShapeRatherThanItsWorldAabb)
{
    PhysicsWorld world;
    BodyDesc sensor;
    sensor.Shape = CollisionShape::MakeBox(Vec3d(3, 2, 0.2f));
    sensor.Rotation = Quatf::FromAxisAngle(Vec3d(0, 1, 0), std::numbers::pi_v<float> / 4);
    sensor.IsTrigger = true;
    sensor.Layer = CollisionLayer::Trigger;
    sensor.UserData = PackEntity(kSensor);
    ASSERT_TRUE(world.AddBody(sensor).IsValid());
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(1.5f, 0, 1.5f));
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    EXPECT_TRUE(overlaps.Pairs().empty());
    mover.SetPosition(sensor.Rotation.RotateVector(Vec3d(1.5f, 0, 0)));
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
}

TEST(SensorPhysics, SolidGeometryAndStaticBodiesDoNotProduceSensorPairs)
{
    PhysicsWorld world;
    BodyDesc solid;
    solid.Layer = CollisionLayer::Static;
    solid.UserData = PackEntity(kSensor);
    const auto body = world.AddBody(solid);
    ASSERT_TRUE(body.IsValid());
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d::Zero());
    SensorOverlaps overlaps;
    overlaps.Begin();
    mover.ObserveSensors(kActor, overlaps);
    world.ObserveSensors(overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    EXPECT_TRUE(overlaps.Occurrences().empty());
}

namespace
{
class SensorScheduleTest : public testing::Test
{
protected:
    SensorScheduleTest()
    {
        Entities.RegisterComponent<LocalTransform>();
        RegisterPhysicsComponents(Entities);
        RegisterPhysics(Schedule);
        Partitions.Add(StoragePartitionId::Default());
        Partitions.Add(StoragePartitionId{ 1 });
        Schedule.Init();
    }

    EntityId Spawn(bool sensor, StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = Entities.CreateEntity(partition);
        Entities.AddComponent<LocalTransform>(entity, LocalTransform{});
        if (sensor)
            Entities.AddComponent<Collider>(entity, Collider{ .IsTrigger = true });
        else
            Entities.AddComponent<CharacterController>(entity, CharacterController{});
        return entity;
    }

    const SensorOverlaps& Tick()
    {
        PhysicsContext context{ Config, Runtime, { 1.0 / 60.0, ++TickIndex }, Entities, Partitions };
        Schedule.RunPhysics(context);
        return Entities.GetResource<SensorOverlaps>();
    }

    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    World Entities;
    EngineSchedule Schedule;
    StoragePartitionSet Partitions;
    std::uint64_t TickIndex = 0;
};
}

TEST_F(SensorScheduleTest, MultipleCharactersAreIndependentAndDestructionHasNoExit)
{
    const auto sensor = Spawn(true);
    const auto a = Spawn(false);
    const auto b = Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 2u);
    Entities.DestroyEntity(a);
    const auto& overlaps = Tick();
    ASSERT_EQ(overlaps.Occurrences().size(), 1u);
    EXPECT_EQ(overlaps.Occurrences()[0].Pair, (SensorPair{ sensor, b }));
    EXPECT_EQ(overlaps.Occurrences()[0].Transition, SensorTransition::Stay);
    Entities.DestroyEntity(sensor);
    EXPECT_TRUE(Tick().Occurrences().empty());
    EXPECT_TRUE(Entities.GetResource<SensorOverlaps>().Pairs().empty());
}

TEST_F(SensorScheduleTest, DepartingZoneIsForgottenAndReturnBeginsWithEnter)
{
    const auto sensor = Spawn(true, StoragePartitionId{ 1 });
    const auto actor = Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    ZoneParticipation active;
    active.Physics = true;
    const ZoneResidencyChange change{ ZoneResidencyChangeKind::ParticipationChanged, ZoneId{ 1 },
                                     StoragePartitionId{ 1 }, active, ZoneParticipation{} };
    ZoneResidencyContext context{ Config, Entities, std::span(&change, 1) };
    Schedule.RunZoneResidency(context);
    EXPECT_TRUE(Entities.GetResource<SensorOverlaps>().Pairs().empty());
    EXPECT_TRUE(Entities.GetResource<SensorOverlaps>().Occurrences().empty());
    Partitions.Remove(StoragePartitionId{ 1 });
    EXPECT_TRUE(Tick().Occurrences().empty());
    Partitions.Add(StoragePartitionId{ 1 });
    const auto& overlaps = Tick();
    ASSERT_EQ(overlaps.Occurrences().size(), 1u);
    EXPECT_EQ(overlaps.Occurrences()[0].Pair, (SensorPair{ sensor, actor }));
    EXPECT_EQ(overlaps.Occurrences()[0].Transition, SensorTransition::Enter);
}

TEST_F(SensorScheduleTest, StaticSensorRelocationFollowsAuthoredTransform)
{
    const auto sensor = Spawn(true);
    (void)Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    Entities.TryGet<LocalTransform>(sensor)->Value.Position = Vec3d(10, 0, 0);
    const auto& departure = Tick();
    ASSERT_EQ(departure.Occurrences().size(), 1u);
    EXPECT_EQ(departure.Occurrences()[0].Transition, SensorTransition::Exit);
    Entities.TryGet<LocalTransform>(sensor)->Value.Position = Vec3d::Zero();
    const auto& arrival = Tick();
    ASSERT_EQ(arrival.Occurrences().size(), 1u);
    EXPECT_EQ(arrival.Occurrences()[0].Transition, SensorTransition::Enter);
}

TEST_F(SensorScheduleTest, SensorPoseUsesThisTicksParentTransform)
{
    Entities.RegisterComponent<WorldTransform>();
    Entities.RegisterComponent<Parent>();
    const auto parent = Entities.CreateEntity();
    Entities.AddComponent<LocalTransform>(parent, LocalTransform{});
    Entities.AddComponent<WorldTransform>(parent, WorldTransform{});
    const auto sensor = Spawn(true);
    Entities.AddComponent<WorldTransform>(sensor, WorldTransform{});
    Entities.AddComponent<Parent>(sensor, Parent{ parent });
    (void)Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    Entities.TryGet<LocalTransform>(parent)->Value.Position = Vec3d(10, 0, 0);
    const auto& departure = Tick();
    ASSERT_EQ(departure.Occurrences().size(), 1u);
    EXPECT_EQ(departure.Occurrences()[0].Transition, SensorTransition::Exit);
}

namespace
{
PhysicsBodyId AddActor(PhysicsWorld& world, Vec3d position, BodyMotion motion = BodyMotion::Dynamic)
{
    BodyDesc desc;
    desc.Position = position;
    desc.Motion = motion;
    desc.Shape = CollisionShape::MakeSphere(0.25f);
    desc.GravityScale = 0;
    desc.LinearDamping = 0;
    desc.UserData = PackEntity(kActor);
    return world.AddBody(desc);
}

void Sample(PhysicsWorld& world, SensorOverlaps& overlaps, std::uint64_t tick)
{
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    ASSERT_TRUE(overlaps.Commit(tick));
}
}

TEST(SensorPhysics, IntegratedDynamicTraversalPublishesLifecycle)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    const auto actor = AddActor(world, Vec3d(-2, 0, 0));
    world.SetLinearVelocity(actor, Vec3d(20, 0, 0));
    SensorOverlaps overlaps;
    Sample(world, overlaps, 0);
    EXPECT_TRUE(overlaps.Occurrences().empty());
    world.Step(0.1f);
    Sample(world, overlaps, 1);
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    world.SetLinearVelocity(actor, Vec3d::Zero());
    world.Step(0.1f);
    Sample(world, overlaps, 2);
    ExpectTransitions(overlaps, { SensorTransition::Stay });
    world.SetLinearVelocity(actor, Vec3d(20, 0, 0));
    world.Step(0.1f);
    Sample(world, overlaps, 3);
    ExpectTransitions(overlaps, { SensorTransition::Exit });
}

TEST(SensorPhysics, DrivenPoseTeleportSkipsSpaceButKeepsDestinationOccupancy)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    const auto actor = AddActor(world, Vec3d(-5, 0, 0));
    DrivenPoseConstraintDesc desc;
    desc.Follower = actor;
    const auto constraint = world.AddDrivenPoseConstraint(desc);
    ASSERT_TRUE(constraint.IsValid());
    SensorOverlaps overlaps;
    Sample(world, overlaps, 0);
    for (auto [position, expected] : std::array{
             std::pair{ Vec3d(5, 0, 0), std::vector<SensorTransition>{} },
             std::pair{ Vec3d::Zero(), std::vector{ SensorTransition::Enter } },
             std::pair{ Vec3d(-5, 0, 0), std::vector{ SensorTransition::Exit } } })
    {
        world.SetDrivenPoseTarget(constraint, {
            .WorldFrame = { position, Quatf::Identity() }, .Teleported = true });
        world.Step(1.0f / 60);
        Sample(world, overlaps, 1);
        ASSERT_EQ(overlaps.Occurrences().size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            EXPECT_EQ(overlaps.Occurrences()[i].Transition, expected[i]);
    }
}

TEST_F(SensorScheduleTest, DynamicSensorDoesNotFollowAuthoredPoseWrites)
{
    const auto sensor = Spawn(true);
    Entities.AddComponent<RigidBody>(sensor, RigidBody{ .Motion = BodyMotion::Dynamic, .GravityScale = 0 });
    const auto actor = Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    Entities.TryGet<LocalTransform>(sensor)->Value.Position = Vec3d(10, 0, 0);
    const auto& overlaps = Tick();
    ASSERT_EQ(overlaps.Occurrences().size(), 1u);
    EXPECT_EQ(overlaps.Occurrences()[0].Pair, (SensorPair{ sensor, actor }));
    EXPECT_EQ(overlaps.Occurrences()[0].Transition, SensorTransition::Stay);
}

TEST_F(SensorScheduleTest, ReusedActivatorIndexBeginsANewPair)
{
    const auto sensor = Spawn(true);
    const auto first = Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    Entities.DestroyEntity(first);
    const auto second = Spawn(false);
    ASSERT_EQ(first.Index, second.Index);
    ASSERT_NE(first.Generation, second.Generation);
    const auto& overlaps = Tick();
    ASSERT_EQ(overlaps.Occurrences().size(), 1u);
    EXPECT_EQ(overlaps.Occurrences()[0].Pair, (SensorPair{ sensor, second }));
    EXPECT_EQ(overlaps.Occurrences()[0].Transition, SensorTransition::Enter);
}

TEST_F(SensorScheduleTest, CharacterSnapAndReconciliationSkipCorrectedSpace)
{
    const auto sensor = Spawn(true);
    Entities.TryGet<LocalTransform>(sensor)->Value.Position = Vec3d::Zero();
    const auto actor = Spawn(false);
    Entities.TryGet<LocalTransform>(actor)->Value.Position = Vec3d(-5, 0, 0);
    EXPECT_TRUE(Tick().Pairs().empty());
    auto& movers = Schedule.Get<PhysicsStepSystem>()->GetCharacterMovers();
    ASSERT_TRUE(movers.SetPosition(Entities, actor, Vec3d(5, 0, 0)));
    EXPECT_TRUE(Tick().Occurrences().empty());
    ASSERT_TRUE(movers.RestorePosition(Entities, actor, Vec3d(-5, 0, 0)));
    EXPECT_TRUE(Tick().Occurrences().empty());
    ASSERT_TRUE(movers.RestorePosition(Entities, actor, Vec3d::Zero()));
    const auto& arrival = Tick();
    ASSERT_EQ(arrival.Occurrences().size(), 1u);
    EXPECT_EQ(arrival.Occurrences()[0].Transition, SensorTransition::Enter);
    ASSERT_TRUE(movers.SetPosition(Entities, actor, Vec3d(5, 0, 0)));
    const auto& departure = Tick();
    ASSERT_EQ(departure.Occurrences().size(), 1u);
    EXPECT_EQ(departure.Occurrences()[0].Transition, SensorTransition::Exit);
}

TEST(SensorPhysics, BackendOverflowPreservesLastCompleteOccupancy)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    ASSERT_TRUE(AddActor(world, Vec3d::Zero(), BodyMotion::Kinematic).IsValid());
    BodyDesc desc;
    desc.Position = Vec3d(5, 0, 0);
    desc.Motion = BodyMotion::Kinematic;
    desc.UserData = PackEntity({ 30, 4 });
    const auto second = world.AddBody(desc);
    SensorOverlaps overlaps(1);
    Sample(world, overlaps, 0);
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    world.SetBodyTransform(second, Vec3d::Zero(), Quatf::Identity());
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    EXPECT_FALSE(overlaps.Commit(1));
    EXPECT_EQ(overlaps.OverflowCount(), 1u);
    EXPECT_TRUE(overlaps.Occurrences().empty());
    ASSERT_EQ(overlaps.Pairs().size(), 1u);
    EXPECT_EQ(overlaps.Pairs()[0], kPair);
}

TEST(SensorPhysics, AddingASensorDoesNotSweepMovementFromAnEarlierSample)
{
    PhysicsWorld world;
    const auto actor = AddActor(world, Vec3d(-5, 0, 0), BodyMotion::Kinematic);
    world.SetBodyTransform(actor, Vec3d(5, 0, 0), Quatf::Identity());
    SensorOverlaps overlaps;
    Sample(world, overlaps, 0);
    ASSERT_TRUE(AddSensor(world).IsValid());
    Sample(world, overlaps, 1);
    EXPECT_TRUE(overlaps.Occurrences().empty());
}

TEST_F(SensorScheduleTest, ParentedKinematicSensorKeepsWorldPose)
{
    Entities.RegisterComponent<WorldTransform>();
    Entities.RegisterComponent<Parent>();
    const auto parent = Entities.CreateEntity();
    Entities.AddComponent<LocalTransform>(parent, LocalTransform{});
    Entities.AddComponent<WorldTransform>(parent, WorldTransform{});
    const auto sensor = Spawn(true);
    Entities.AddComponent<WorldTransform>(sensor, WorldTransform{});
    Entities.AddComponent<Parent>(sensor, Parent{ parent });
    Entities.AddComponent<RigidBody>(sensor, RigidBody{ .Motion = BodyMotion::Kinematic });
    (void)Spawn(false);
    ASSERT_EQ(Tick().Pairs().size(), 1u);
    Entities.TryGet<LocalTransform>(parent)->Value.Position = Vec3d(10, 0, 0);
    const auto& departure = Tick();
    ASSERT_EQ(departure.Occurrences().size(), 1u);
    EXPECT_EQ(departure.Occurrences()[0].Transition, SensorTransition::Exit);
}

TEST(SensorPhysics, BodyTeleportSkipsSpaceAndTransitionsAtDestination)
{
    for (const auto motion : { BodyMotion::Dynamic, BodyMotion::Kinematic })
    {
        PhysicsWorld world;
        ASSERT_TRUE(AddSensor(world).IsValid());
        const auto actor = AddActor(world, Vec3d(-5, 0, 0), motion);
        SensorOverlaps overlaps;
        Sample(world, overlaps, 0);
        world.TeleportBody(actor, Vec3d(5, 0, 0), Quatf::Identity());
        Sample(world, overlaps, 1);
        EXPECT_TRUE(overlaps.Occurrences().empty());
        world.TeleportBody(actor, Vec3d::Zero(), Quatf::Identity());
        Sample(world, overlaps, 2);
        ExpectTransitions(overlaps, { SensorTransition::Enter });
        world.TeleportBody(actor, Vec3d(-5, 0, 0), Quatf::Identity());
        Sample(world, overlaps, 3);
        ExpectTransitions(overlaps, { SensorTransition::Exit });
        world.SetBodyTransform(actor, Vec3d(5, 0, 0), Quatf::Identity());
        Sample(world, overlaps, 4);
        ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
    }
}

TEST(SensorPhysics, MovementAfterTeleportStillSweepsFromTheRelocatedPose)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    const auto actor = AddActor(world, Vec3d(5, 0, 0));
    SensorOverlaps overlaps;
    Sample(world, overlaps, 0);
    world.SetLinearVelocity(actor, Vec3d(100, 0, 0));
    world.TeleportBody(actor, Vec3d(-5, 0, 0), Quatf::Identity());
    EXPECT_EQ(world.GetLinearVelocity(actor), Vec3d(100, 0, 0));
    world.Step(0.1f);
    Sample(world, overlaps, 1);
    ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
}

TEST(SensorPhysics, DrivenPoseMovementAfterTeleportStillSweeps)
{
    PhysicsWorld world;
    ASSERT_TRUE(AddSensor(world).IsValid());
    const auto actor = AddActor(world, Vec3d(5, 0, 0));
    DrivenPoseConstraintDesc desc;
    desc.Follower = actor;
    const auto constraint = world.AddDrivenPoseConstraint(desc);
    DrivenPoseTarget target;
    target.WorldFrame = { Vec3d(-5, 0, 0), Quatf::Identity() };
    target.Teleported = true;
    target.LinearVelocity = Vec3d(100, 0, 0);
    world.SetDrivenPoseTarget(constraint, target);
    world.Step(0.1f);
    SensorOverlaps overlaps;
    Sample(world, overlaps, 1);
    ExpectTransitions(overlaps, { SensorTransition::Enter, SensorTransition::Exit });
}

TEST_F(SensorScheduleTest, KinematicAuthoredMovementKeepsFastCrossings)
{
    const auto sensor = Spawn(true);
    const auto actor = Entities.CreateEntity();
    LocalTransform transform;
    transform.Value.Position = Vec3d(-5, 0, 0);
    Entities.AddComponent<LocalTransform>(actor, transform);
    Entities.AddComponent<Collider>(actor, Collider{});
    Entities.AddComponent<RigidBody>(actor, RigidBody{ .Motion = BodyMotion::Kinematic });
    EXPECT_TRUE(Tick().Occurrences().empty());
    Entities.TryGet<LocalTransform>(actor)->Value.Position = Vec3d(5, 0, 0);
    const auto& crossing = Tick();
    ASSERT_EQ(crossing.Occurrences().size(), 2u);
    EXPECT_EQ(crossing.Occurrences()[0].Pair, (SensorPair{ sensor, actor }));
    EXPECT_EQ(crossing.Occurrences()[0].Transition, SensorTransition::Enter);
    EXPECT_EQ(crossing.Occurrences()[1].Transition, SensorTransition::Exit);
}

TEST(SensorPhysics, EmptySensorSceneSkipsQueriesButAdvancesBodyAndCharacterHistory)
{
    PhysicsWorld world;
    const auto actor = AddActor(world, Vec3d(-5, 0, 0), BodyMotion::Kinematic);
    CharacterMover mover(world, CharacterMoverConfig{}, Vec3d(-5, 0, 1.5f));
    CharacterMoveRequest move;
    move.DeltaSeconds = 0.1f;
    move.Velocity = Vec3d(100, 0, 0);
    (void)mover.Move(move);
    world.SetBodyTransform(actor, Vec3d(5, 0, 0), Quatf::Identity());
    SensorOverlaps overlaps;
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    mover.ObserveSensors({ 30, 4 }, overlaps);
    ASSERT_TRUE(overlaps.Commit(0));
    EXPECT_EQ(overlaps.QueryCounts().Overlaps, 0u);
    EXPECT_EQ(overlaps.QueryCounts().Casts, 0u);
    const auto sensor = AddSensor(world);
    ASSERT_TRUE(sensor.IsValid());
    EXPECT_EQ(world.SensorCount(), 1u);
    overlaps.Begin();
    world.ObserveSensors(overlaps);
    mover.ObserveSensors({ 30, 4 }, overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    EXPECT_EQ(overlaps.QueryCounts().Overlaps, 2u);
    EXPECT_EQ(overlaps.QueryCounts().Casts, 0u);
    EXPECT_TRUE(overlaps.Occurrences().empty());
    world.RemoveBody(sensor);
    EXPECT_EQ(world.SensorCount(), 0u);
    Sample(world, overlaps, 2);
    EXPECT_EQ(overlaps.QueryCounts().Overlaps, 0u);
}

TEST(SensorPhysics, SleepingBodyStillObservesRelocatedSensor)
{
    PhysicsWorld world;
    const auto sensor = AddSensor(world, Vec3d(5, 0, 0));
    const auto actor = AddActor(world, Vec3d::Zero());
    for (int i = 0; i < 120; ++i)
        world.Step(1.0f / 60);
    ASSERT_FALSE(world.IsBodyActive(actor));
    SensorOverlaps overlaps;
    Sample(world, overlaps, 0);
    EXPECT_TRUE(overlaps.Occurrences().empty());
    world.SetBodyTransform(sensor, Vec3d::Zero(), Quatf::Identity());
    Sample(world, overlaps, 1);
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    world.SetBodyTransform(sensor, Vec3d(5, 0, 0), Quatf::Identity());
    Sample(world, overlaps, 2);
    ExpectTransitions(overlaps, { SensorTransition::Exit });
}
