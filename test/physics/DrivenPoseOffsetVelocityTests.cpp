#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>

TEST(PhysicsConstraint, OffCenterFollowerGetsBodyCenterFeedForward)
{
    constexpr float dt = 1.0f / 60.0f;

    PhysicsWorld world;

    BodyDesc body;
    body.Shape = CollisionShape::MakeBox(Vec3d(0.25f, 0.25f, 0.25f));
    body.Position = Vec3d(-1.0f, 5.0f, 0.0f);
    body.Motion = BodyMotion::Dynamic;
    body.Layer = CollisionLayer::Moving;
    body.Mass = 1.0f;
    body.GravityScale = 0.0f;
    const PhysicsBodyId follower = world.AddBody(body);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    desc.FollowerLocalFrame.Position = Vec3d(1.0f, 0.0f, 0.0f);
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    ASSERT_TRUE(constraint.IsValid());

    DrivenPoseTarget target;
    target.WorldFrame.Position = Vec3d(0.0f, 5.0f, 0.0f);
    target.WorldFrame.Rotation = Quatf::Identity();
    target.AngularVelocity = Vec3d(0.0f, 0.8f, 0.0f);

    // At zero pose error, a +Y angular velocity around a frame one metre to the
    // body's +X requires +Z body-center velocity: v_body = v_frame - w x r.
    world.SetDrivenPoseTarget(constraint, target);
    world.Step(dt);

    EXPECT_NEAR(world.GetLinearVelocity(follower).X, 0.0f, 0.01f);
    EXPECT_NEAR(world.GetLinearVelocity(follower).Z, 0.8f, 0.05f);
    EXPECT_NEAR(
        world.GetConstraintTelemetry(constraint).CommandedLinearSpeed,
        0.8f,
        0.05f);
}
