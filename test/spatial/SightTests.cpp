// Sight over physics raycasts: range, then cone, then ray, with the target's
// own body counting as reached.

#include <spatial/sight/SightTest.h>

#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>

#include <gtest/gtest.h>

#include <numbers>

namespace
{
    const EntityId kWall{ 3, 1 };
    const EntityId kTarget{ 4, 1 };
    const EntityId kSensor{ 5, 1 };

    void AddBox(PhysicsWorld& world, Vec3d position, Vec3d halfExtents, EntityId entity, bool trigger = false)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(halfExtents);
        body.Position = position;
        body.Motion = BodyMotion::Static;
        body.Layer = trigger ? CollisionLayer::Trigger : CollisionLayer::Static;
        body.IsTrigger = trigger;
        body.UserData = PackEntity(entity);
        (void)world.AddBody(body);
    }

    SightObserver EyeAtOriginLookingPlusX(float halfAngle = std::numbers::pi_v<float>, float range = 100.0f)
    {
        return SightObserver{ .Eye = Vec3d::Zero(), .Forward = Vec3d(1, 0, 0), .HalfAngle = halfAngle, .Range = range };
    }
}

TEST(Sight, ClearLineIsSeen)
{
    PhysicsWorld world;
    PhysicsQueries queries(world);
    const SightResult result = TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), {}, {});
    EXPECT_TRUE(result.Seen());
    EXPECT_TRUE(result.CastRay);
}

TEST(Sight, WallBlocksAndReportsTheHit)
{
    PhysicsWorld world;
    AddBox(world, Vec3d(5, 0, 0), Vec3d(0.5f, 2, 2), kWall);
    PhysicsQueries queries(world);

    const SightResult result = TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), {}, {});

    EXPECT_EQ(result.Outcome, SightOutcome::Blocked);
    EXPECT_EQ(result.HitEntity, kWall);
    EXPECT_NEAR(result.HitPoint.X, 4.5f, 0.05f);
}

TEST(Sight, TargetsOwnBodyCountsAsSeen)
{
    PhysicsWorld world;
    AddBox(world, Vec3d(10, 0, 0), Vec3d(1, 1, 1), kTarget);
    PhysicsQueries queries(world);

    EXPECT_TRUE(TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), kTarget, {}).Seen());
    EXPECT_EQ(TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), {}, {}).Outcome, SightOutcome::Blocked);
}

TEST(Sight, RangeAndConeRejectWithoutARay)
{
    PhysicsWorld world;
    PhysicsQueries queries(world);

    const SightResult far = TestSight(queries, EyeAtOriginLookingPlusX(std::numbers::pi_v<float>, 5.0f),
                                      Vec3d(10, 0, 0), {}, {});
    EXPECT_EQ(far.Outcome, SightOutcome::OutOfRange);
    EXPECT_FALSE(far.CastRay);

    const float quarterTurn = std::numbers::pi_v<float> / 4.0f;
    const SightResult behind = TestSight(queries, EyeAtOriginLookingPlusX(quarterTurn), Vec3d(-10, 0, 0), {}, {});
    EXPECT_EQ(behind.Outcome, SightOutcome::OutsideCone);
    EXPECT_FALSE(behind.CastRay);

    EXPECT_TRUE(TestSight(queries, EyeAtOriginLookingPlusX(quarterTurn), Vec3d(10, 0, 5), {}, {}).Seen());
}

TEST(Sight, TriggersDoNotBlockUnlessTheFilterIncludesThem)
{
    PhysicsWorld world;
    AddBox(world, Vec3d(5, 0, 0), Vec3d(0.5f, 2, 2), kSensor, true);
    PhysicsQueries queries(world);

    EXPECT_TRUE(TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), {}, {}).Seen());
    EXPECT_EQ(TestSight(queries, EyeAtOriginLookingPlusX(), Vec3d(10, 0, 0), {}, PhysicsQueryFilter{ .IncludeTriggers = true })
                  .Outcome,
              SightOutcome::Blocked);
}
