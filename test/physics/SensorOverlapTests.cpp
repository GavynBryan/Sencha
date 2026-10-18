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
    world.ObserveBodySensors(body, Vec3d::Zero(), overlaps);
    ASSERT_TRUE(overlaps.Commit(1));
    ExpectTransitions(overlaps, { SensorTransition::Enter });
    world.SetBodyTransform(body, Vec3d(5, 0, 0), Quatf::Identity());
    overlaps.Begin();
    world.ObserveBodySensors(body, Vec3d::Zero(), overlaps);
    ASSERT_TRUE(overlaps.Commit(2));
    ExpectTransitions(overlaps, { SensorTransition::Exit });
    world.SetBodyTransform(body, Vec3d(-5, 0, 0), Quatf::Identity());
    overlaps.Begin();
    world.ObserveBodySensors(body, Vec3d(5, 0, 0), overlaps);
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
    world.ObserveBodySensors(body, Vec3d::Zero(), overlaps);
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
