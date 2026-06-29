// Read-only spatial queries (raycast / sweep / overlap) over a PhysicsWorld.
// Bodies carry a packed EntityId in user data; queries report that entity.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <physics/PhysicsQueries.h>
#include <physics/PhysicsScene.h> // PackEntity
#include <physics/PhysicsWorld.h>

namespace
{
const EntityId kBoxEntity{ 7, 1 };

PhysicsBodyId AddBoxAtOrigin(PhysicsWorld& world)
{
    BodyDesc box;
    box.Shape = CollisionShape::MakeBox(Vec3d(1.0f, 1.0f, 1.0f)); // top face at y = 1
    box.Position = Vec3d::Zero();
    box.Motion = BodyMotion::Static;
    box.Layer = CollisionLayer::Static;
    box.UserData = PackEntity(kBoxEntity);
    return world.AddBody(box);
}
} // namespace

TEST(PhysicsQueries, RaycastHitsBoxAndReportsEntity)
{
    PhysicsWorld world;
    AddBoxAtOrigin(world);
    PhysicsQueries queries(world);

    const RaycastHit hit = queries.Raycast(Vec3d(0.0f, 5.0f, 0.0f), Vec3d(0.0f, -1.0f, 0.0f), 10.0f);

    ASSERT_TRUE(hit.Hit);
    EXPECT_EQ(hit.Entity, kBoxEntity);
    EXPECT_NEAR(hit.Distance, 4.0f, 0.05f); // y 5 -> box top y 1
    EXPECT_NEAR(hit.Point.Y, 1.0f, 0.05f);
    EXPECT_NEAR(hit.Normal.Y, 1.0f, 0.05f);
}

TEST(PhysicsQueries, RaycastMissesWhenOffTarget)
{
    PhysicsWorld world;
    AddBoxAtOrigin(world);
    PhysicsQueries queries(world);

    const RaycastHit hit = queries.Raycast(Vec3d(10.0f, 5.0f, 0.0f), Vec3d(0.0f, -1.0f, 0.0f), 10.0f);

    EXPECT_FALSE(hit.Hit);
}

TEST(PhysicsQueries, OverlapShapeFindsBox)
{
    PhysicsWorld world;
    AddBoxAtOrigin(world);
    PhysicsQueries queries(world);

    std::vector<EntityId> hits;
    queries.OverlapShape(CollisionShape::MakeSphere(2.0f), Vec3d::Zero(), Quatf::Identity(), hits);

    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], kBoxEntity);
}

TEST(PhysicsQueries, OverlapShapeEmptyWhenDisjoint)
{
    PhysicsWorld world;
    AddBoxAtOrigin(world);
    PhysicsQueries queries(world);

    std::vector<EntityId> hits;
    queries.OverlapShape(CollisionShape::MakeSphere(0.5f), Vec3d(20.0f, 0.0f, 0.0f), Quatf::Identity(), hits);

    EXPECT_TRUE(hits.empty());
}

TEST(PhysicsQueries, SweepSphereHitsBoxTop)
{
    PhysicsWorld world;
    AddBoxAtOrigin(world);
    PhysicsQueries queries(world);

    const ShapeSweepHit hit = queries.SweepShape(
        CollisionShape::MakeSphere(0.5f),
        Vec3d(0.0f, 5.0f, 0.0f),
        Quatf::Identity(),
        Vec3d(0.0f, -1.0f, 0.0f),
        10.0f);

    ASSERT_TRUE(hit.Hit);
    EXPECT_EQ(hit.Entity, kBoxEntity);
    // Sphere (r 0.5) center stops at box top (y 1) + radius = y 1.5, i.e. 3.5 of 10.
    EXPECT_NEAR(hit.Fraction, 0.35f, 0.03f);
    EXPECT_NEAR(hit.Normal.Y, 1.0f, 0.05f);
}
