#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>
#include <physics/PhysicsQueries.h>
#include <physics/RigidBodyBinding.h>
#include <physics/SensorOverlaps.h>

#include <limits>
#include <numbers>

namespace
{
constexpr EntityId kSensor{ 13, 4 }, kActor{ 21, 7 };

PhysicsBodyId Sensor(PhysicsWorld& world)
{
    BodyDesc desc;
    desc.Position = { -3, 0, 0 };
    desc.Shape = CollisionShape::MakeBox({ 0.5f, 2, 2 });
    desc.ShapePose.Position = { 3, 0, 0 };
    desc.IsTrigger = true;
    desc.Layer = CollisionLayer::Trigger;
    desc.UserData = PackEntity(kSensor);
    return world.AddBody(desc);
}

void Observe(PhysicsWorld& world, SensorOverlaps& observations, std::uint64_t tick)
{
    observations.Begin();
    world.ObserveSensors(observations);
    ASSERT_TRUE(observations.Commit(tick));
}
}

TEST(ColliderShapePose, RotatesAndOffsetsShapeWithoutMovingBodyOrigin)
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.Shape = CollisionShape::MakeBox({ 2, 0.25f, 0.5f });
    desc.Position = { 8, 0, 0 };
    desc.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float> * 0.5f);
    desc.ShapePose = { { 3, 1, 0 }, Quatf::FromAxisAngle({ 0, 0, 1 }, std::numbers::pi_v<float> * 0.5f) };
    const auto body = world.AddBody(desc);
    ASSERT_TRUE(body.IsValid());
    EXPECT_NEAR((world.GetBodyTransform(body).Position - desc.Position).Magnitude(), 0, 1e-5f);
    EXPECT_TRUE(world.GetBodyTransform(body).Rotation.NearlyEquals(desc.Rotation));
    const Vec3d center = desc.Position + desc.Rotation.RotateVector(desc.ShapePose.Position);
    const auto hit = PhysicsQueries(world).Raycast(center + Vec3d{ 5, 0, 0 }, { -1, 0, 0 }, 10);
    ASSERT_TRUE(hit.Hit);
    EXPECT_NEAR(hit.Distance, 4.5f, 1e-4f);
    world.TeleportBody(body, desc.Position + Vec3d{ 0, 0, 10 }, desc.Rotation);
    EXPECT_EQ(world.GetBodyTransform(body).Position, desc.Position + Vec3d(0, 0, 10));
    EXPECT_TRUE(PhysicsQueries(world).Raycast(center + Vec3d{ 5, 0, 10 }, { -1, 0, 0 }, 10).Hit);
}

TEST(ColliderShapePose, OffsetActivatorsStillCrossAndTeleportWithoutSweepingSkippedSpace)
{
    PhysicsWorld world;
    ASSERT_TRUE(Sensor(world).IsValid());
    BodyDesc desc;
    desc.Position = { -10, 0, 0 };
    desc.Shape = CollisionShape::MakeSphere(0.25f);
    desc.ShapePose.Position = { 3, 0, 0 };
    desc.Motion = BodyMotion::Kinematic;
    desc.UserData = PackEntity(kActor);
    const auto actor = world.AddBody(desc);
    ASSERT_TRUE(actor.IsValid());
    SensorOverlaps observations;
    Observe(world, observations, 1);
    EXPECT_TRUE(observations.Occurrences().empty());
    world.SetBodyTransform(actor, { 10, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 2);
    ASSERT_EQ(observations.Occurrences().size(), 2u);
    EXPECT_EQ(observations.Occurrences()[0].Transition, SensorTransition::Enter);
    EXPECT_EQ(observations.Occurrences()[1].Transition, SensorTransition::Exit);
    world.TeleportBody(actor, { -10, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 3);
    EXPECT_TRUE(observations.Occurrences().empty());
    world.TeleportBody(actor, { -3, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 4);
    ASSERT_EQ(observations.Pairs().size(), 1u);
    world.TeleportBody(actor, { 10, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 5);
    ASSERT_EQ(observations.Occurrences().size(), 1u);
    EXPECT_EQ(observations.Occurrences()[0].Transition, SensorTransition::Exit);
}

TEST(ColliderShapePose, RelocatedSensorUsesItsOffsetShapeForEndpointOccupancy)
{
    PhysicsWorld world;
    const auto sensor = Sensor(world);
    ASSERT_TRUE(sensor.IsValid());
    BodyDesc desc;
    desc.Shape = CollisionShape::MakeSphere(0.25f);
    desc.Motion = BodyMotion::Kinematic;
    desc.UserData = PackEntity(kActor);
    ASSERT_TRUE(world.AddBody(desc).IsValid());
    SensorOverlaps observations;
    Observe(world, observations, 1);
    ASSERT_EQ(observations.Pairs().size(), 1u);
    world.SetBodyTransform(sensor, { 7, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 2);
    ASSERT_EQ(observations.Occurrences().size(), 1u);
    EXPECT_EQ(observations.Occurrences()[0].Transition, SensorTransition::Exit);
    world.SetBodyTransform(sensor, { -3, 0, 0 }, Quatf::Identity());
    Observe(world, observations, 3);
    ASSERT_EQ(observations.Occurrences().size(), 1u);
    EXPECT_EQ(observations.Occurrences()[0].Transition, SensorTransition::Enter);
}

TEST(ColliderShapePose, DrivenTeleportKeepsOriginAndDoesNotInventCrossings)
{
    PhysicsWorld world(PhysicsWorldConfig{ .Gravity = Vec3d::Zero() });
    ASSERT_TRUE(Sensor(world).IsValid());
    BodyDesc desc;
    desc.Position = { -10, 0, 0 };
    desc.Shape = CollisionShape::MakeSphere(0.25f);
    desc.ShapePose.Position = { 0, 1, 0 };
    desc.Motion = BodyMotion::Dynamic;
    desc.UserData = PackEntity(kActor);
    const auto body = world.AddBody(desc);
    ASSERT_TRUE(body.IsValid());
    DrivenPoseConstraintDesc drive;
    drive.Follower = body;
    const auto constraint = world.AddDrivenPoseConstraint(drive);
    ASSERT_TRUE(constraint.IsValid());
    SensorOverlaps observations;
    Observe(world, observations, 1);
    world.SetDrivenPoseTarget(constraint, { .WorldFrame = { { 10, 0, 0 }, Quatf::Identity() }, .Teleported = true });
    world.Step(1.0f / 60.0f);
    Observe(world, observations, 2);
    EXPECT_TRUE(observations.Occurrences().empty());
    EXPECT_NEAR((world.GetBodyTransform(body).Position - Vec3d{ 10, 0, 0 }).Magnitude(), 0, 1e-5f);
}

TEST(ColliderShapePose, RejectsInvalidNewShapePosesAndUnresolvedMeshHandles)
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.ShapePose.Rotation = { 0, 0, 0, 0 };
    EXPECT_FALSE(world.AddBody(desc).IsValid());
    desc.ShapePose.Rotation = Quatf::Identity();
    desc.ShapePose.Position.X = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(world.AddBody(desc).IsValid());
    desc.ShapePose.Position = {};
    desc.MeshShape = CollisionShapeHandle{ 42 };
    EXPECT_FALSE(world.AddBody(desc).IsValid());
}

TEST(ColliderShapePose, DrivenFramesUseCenterOfMassLeverArmsWithAndWithoutTeleportation)
{
    for (const bool teleported : { false, true })
    {
        PhysicsWorld world(PhysicsWorldConfig{ .Gravity = Vec3d::Zero() });
        BodyDesc desc;
        desc.Shape = CollisionShape::MakeSphere(0.25f);
        desc.ShapePose.Position = { 0, 2, 0 };
        desc.Motion = BodyMotion::Dynamic;
        desc.LinearDamping = 0;
        desc.AngularDamping = 0;
        const auto body = world.AddBody(desc);
        ASSERT_TRUE(body.IsValid());
        DrivenPoseConstraintDesc drive;
        drive.Follower = body;
        const auto constraint = world.AddDrivenPoseConstraint(drive);
        ASSERT_TRUE(constraint.IsValid());
        DrivenPoseTarget target;
        target.WorldFrame.Position = teleported ? Vec3d{ 10, 0, 0 } : Vec3d::Zero();
        target.AngularVelocity = { 0, 0, 2 };
        target.Teleported = teleported;
        world.SetDrivenPoseTarget(constraint, target);
        world.Step(1.0f / 120.0f);
        EXPECT_NEAR((world.GetLinearVelocity(body) - Vec3d{ -4, 0, 0 }).Magnitude(), 0, 1e-5f);
        EXPECT_LT((world.GetBodyTransform(body).Position - target.WorldFrame.Position).Magnitude(), 0.001f);
    }
}
