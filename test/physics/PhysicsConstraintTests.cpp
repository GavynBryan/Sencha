// Driven pose constraints at the PhysicsWorld facade: one-way locked velocity
// driving toward a start-of-step target frame, collision preserved,
// generational handles, and body-removal safety in either cleanup order.

#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>

#include <cmath>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

PhysicsBodyId AddFloor(PhysicsWorld& world)
{
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
    floor.Position = Vec3d(0.0f, 0.0f, 0.0f);
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    return world.AddBody(floor);
}

PhysicsBodyId AddDynamicBox(PhysicsWorld& world, const Vec3d& position, float gravityScale = 1.0f)
{
    BodyDesc box;
    box.Shape = CollisionShape::MakeBox(Vec3d(0.25f, 0.25f, 0.25f));
    box.Position = position;
    box.Motion = BodyMotion::Dynamic;
    box.Layer = CollisionLayer::Moving;
    box.Mass = 1.0f;
    box.GravityScale = gravityScale;
    return world.AddBody(box);
}

DrivenPoseTarget FrameAt(const Vec3d& position, const Quatf& rotation = Quatf::Identity(),
                         const Vec3d& linearVelocity = Vec3d::Zero(),
                         const Vec3d& angularVelocity = Vec3d::Zero())
{
    DrivenPoseTarget target;
    target.WorldFrame.Position = position;
    target.WorldFrame.Rotation = rotation;
    target.LinearVelocity = linearVelocity;
    target.AngularVelocity = angularVelocity;
    return target;
}

float Distance(const Vec3d& a, const Vec3d& b)
{
    return (a - b).Magnitude();
}
} // namespace

TEST(PhysicsConstraint, StartOfStepTargetDoesNotDoubleCountFeedForward)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    ASSERT_TRUE(constraint.IsValid());

    const Vec3d frameVelocity(2.0f, 0.0f, 0.0f);
    world.SetDrivenPoseTarget(
        constraint,
        FrameAt(Vec3d(0.0f, 5.0f, 0.0f), Quatf::Identity(), frameVelocity));
    world.Step(kFixedDt);

    const float expectedX = frameVelocity.X * kFixedDt;
    EXPECT_NEAR(world.GetBodyTransform(follower).Position.X, expectedX, 0.01f);
    EXPECT_NEAR(world.GetConstraintTelemetry(constraint).CommandedLinearSpeed, 2.0f, 0.01f);
}

TEST(PhysicsConstraint, LockedPositionFollowsTranslatingFrame)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    ASSERT_TRUE(constraint.IsValid());

    Vec3d framePos(0.0f, 5.0f, 0.0f);
    const Vec3d frameVel(2.0f, 0.0f, 0.0f);
    for (int i = 0; i < 120; ++i)
    {
        world.SetDrivenPoseTarget(constraint, FrameAt(framePos, Quatf::Identity(), frameVel));
        world.Step(kFixedDt);
        framePos = framePos + frameVel * kFixedDt;
        EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, framePos), 0.02f);
    }

    EXPECT_LT(world.GetConstraintTelemetry(constraint).PositionError, 0.02f);
}

TEST(PhysicsConstraint, LockedOrientationFollowsRotatingFrame)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    const Vec3d spin(0.0f, 1.0f, 0.0f);
    float angle = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        const Quatf frameRot = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), angle);
        world.SetDrivenPoseTarget(
            constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f), frameRot, Vec3d::Zero(), spin));
        world.Step(kFixedDt);
        angle += kFixedDt;
    }

    EXPECT_LT(world.GetConstraintTelemetry(constraint).AngularError, 0.05f);
    EXPECT_NEAR(world.GetAngularVelocity(follower).Y, 1.0f, 0.1f);
    EXPECT_NEAR(world.GetConstraintTelemetry(constraint).CommandedAngularSpeed, 1.0f, 0.1f);
}

TEST(PhysicsConstraint, OffCenterFollowerTracesTheFrameRotation)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(-1.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    desc.FollowerLocalFrame.Position = Vec3d(1.0f, 0.0f, 0.0f);
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    const Vec3d pivot(0.0f, 5.0f, 0.0f);
    float angle = 0.0f;
    for (int i = 0; i < 240; ++i)
    {
        const Quatf frameRot = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), angle);
        world.SetDrivenPoseTarget(
            constraint, FrameAt(pivot, frameRot, Vec3d::Zero(), Vec3d(0.0f, 0.8f, 0.0f)));
        world.Step(kFixedDt);
        angle += 0.8f * kFixedDt;
    }

    EXPECT_NEAR(Distance(world.GetBodyTransform(follower).Position, pivot), 1.0f, 0.05f);
}

TEST(PhysicsConstraint, SuspendedFollowerHoldsPoseWithoutGravity)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    for (int i = 0; i < 120; ++i)
    {
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
        world.Step(kFixedDt);
    }

    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(0.0f, 5.0f, 0.0f)), 0.01f);
}

TEST(PhysicsConstraint, FollowerStillCollidesWithWorldGeometry)
{
    PhysicsWorld world;
    AddFloor(world);
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 3.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    for (int i = 0; i < 180; ++i)
    {
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, -2.0f, 0.0f)));
        world.Step(kFixedDt);
    }

    EXPECT_GT(world.GetBodyTransform(follower).Position.Y, 0.6f);
    EXPECT_GT(world.GetConstraintTelemetry(constraint).PositionError, 2.0f);
}

TEST(PhysicsConstraint, TeleportSnapsWithoutSyntheticVelocity)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
    world.Step(kFixedDt);

    DrivenPoseTarget far = FrameAt(Vec3d(100.0f, 5.0f, 0.0f));
    far.Teleported = true;
    world.SetDrivenPoseTarget(constraint, far);
    world.Step(kFixedDt);

    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(100.0f, 5.0f, 0.0f)), 0.01f);
    EXPECT_LT(world.GetLinearVelocity(follower).Magnitude(), 0.01f);
}

TEST(PhysicsConstraint, RemoveInvalidatesAndDeadHandleRemovalIsNoOp)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    EXPECT_TRUE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 1u);

    world.RemoveConstraint(constraint);
    EXPECT_FALSE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 0u);

    world.RemoveConstraint(constraint);
    EXPECT_EQ(world.ConstraintCount(), 0u);
}

TEST(PhysicsConstraint, StaleGenerationNeverResolvesAfterSlotReuse)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId first = world.AddDrivenPoseConstraint(desc);
    world.RemoveConstraint(first);

    const PhysicsConstraintId second = world.AddDrivenPoseConstraint(desc);
    ASSERT_EQ(second.Index, first.Index);
    EXPECT_NE(second.Generation, first.Generation);

    EXPECT_FALSE(world.IsConstraintValid(first));
    EXPECT_TRUE(world.IsConstraintValid(second));

    world.SetDrivenPoseTarget(first, FrameAt(Vec3d(9.0f, 9.0f, 9.0f)));
    world.SetDrivenPoseTarget(second, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
    world.Step(kFixedDt);
    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(0.0f, 5.0f, 0.0f)), 0.5f);
}

TEST(PhysicsConstraint, RemoveBodyInvalidatesDependentConstraints)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    world.RemoveBody(follower);
    EXPECT_FALSE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 0u);

    world.RemoveConstraint(constraint);
    world.Step(kFixedDt);
}

TEST(PhysicsConstraint, CreateDestroyChurnLeaksNothing)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    for (int i = 0; i < 100; ++i)
    {
        DrivenPoseConstraintDesc desc;
        desc.Follower = follower;
        const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
        ASSERT_TRUE(constraint.IsValid());
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
        world.Step(kFixedDt);
        world.RemoveConstraint(constraint);
    }

    EXPECT_EQ(world.ConstraintCount(), 0u);
    EXPECT_EQ(world.StaleRefreshCount(), 0u);
}

#ifndef NDEBUG
TEST(PhysicsConstraint, UnrefreshedConstraintAssertsInDebug)
{
    EXPECT_DEATH(
        {
            PhysicsWorld world;
            const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));
            DrivenPoseConstraintDesc desc;
            desc.Follower = follower;
            (void)world.AddDrivenPoseConstraint(desc);
            world.Step(kFixedDt);
        },
        "not refreshed");
}

TEST(PhysicsConstraint, UnsupportedSpringAssertsInPrototype)
{
    EXPECT_DEATH(
        {
            PhysicsWorld world;
            const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));
            DrivenPoseConstraintDesc desc;
            desc.Follower = follower;
            desc.LinearDrive.Response = PoseDriveResponse::Spring;
            (void)world.AddDrivenPoseConstraint(desc);
        },
        "require P3");
}
#endif

TEST(PhysicsConstraint, DeterministicAcrossIdenticalRuns)
{
    auto run = [](Vec3d& out)
    {
        PhysicsWorld world;
        AddFloor(world);
        const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.1f, 3.0f, -0.2f));

        DrivenPoseConstraintDesc desc;
        desc.Follower = follower;
        const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

        Vec3d framePos(0.1f, 2.0f, -0.2f);
        const Vec3d frameVelocity(1.0f, 0.0f, 0.0f);
        for (int i = 0; i < 120; ++i)
        {
            world.SetDrivenPoseTarget(
                constraint, FrameAt(framePos, Quatf::Identity(), frameVelocity));
            world.Step(kFixedDt);
            framePos = framePos + frameVelocity * kFixedDt;
        }
        out = world.GetBodyTransform(follower).Position;
    };

    Vec3d a;
    Vec3d b;
    run(a);
    run(b);
    EXPECT_EQ(a.X, b.X);
    EXPECT_EQ(a.Y, b.Y);
    EXPECT_EQ(a.Z, b.Z);
}
