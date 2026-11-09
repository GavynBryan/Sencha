// physics.clear_path: a line, or an upright capsule moved along one.

#include <physics/PhysicsAuthoredQueries.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>

#include <authored/AuthoredValue.h>
#include <authored/AuthoredValueTraits.h>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{
    bool Clear(const PhysicsWorld& world, const Vec3d& from, const Vec3d& to, float radius, float height)
    {
        const PhysicsQueries queries(world);
        const std::vector<AuthoredValue> arguments{
            AuthoredValueTraits<Vec3d>::Encode(from), AuthoredValueTraits<Vec3d>::Encode(to),
            AuthoredValue::Float(radius),             AuthoredValue::Float(height),
            AuthoredValue{},
        };
        AuthoredValue result;
        EXPECT_EQ(EvaluatePhysicsClearPath(queries, arguments, result), AuthoredQueryStatus::Value);
        bool clear = false;
        EXPECT_TRUE(result.TryGetBool(clear));
        return clear;
    }

    void Wall(PhysicsWorld& world, const Vec3d& centre, const Vec3d& half)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(half);
        body.Position = centre;
        body.Motion = BodyMotion::Static;
        body.Layer = CollisionLayer::Static;
        (void)world.AddBody(body);
    }
}

TEST(PhysicsClearPath, ALineStopsShortOfTheSurfaceItIsAimedAt)
{
    PhysicsWorld world;
    Wall(world, Vec3d(5.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    EXPECT_TRUE(Clear(world, Vec3d::Zero(), Vec3d(5.0f, 0.0f, 0.0f), 0.0f, 0.0f)) << "the anchor's own wall";
    EXPECT_FALSE(Clear(world, Vec3d::Zero(), Vec3d(8.0f, 0.0f, 0.0f), 0.0f, 0.0f)) << "through it";
}

TEST(PhysicsClearPath, ACapsuleNeedsRoomALineDoesNot)
{
    PhysicsWorld world;
    // A lintel at head height over the path.
    Wall(world, Vec3d(5.0f, 1.2f, 0.0f), Vec3d(0.5f, 0.5f, 3.0f));
    EXPECT_TRUE(Clear(world, Vec3d::Zero(), Vec3d(10.0f, 0.0f, 0.0f), 0.0f, 0.0f));
    EXPECT_FALSE(Clear(world, Vec3d::Zero(), Vec3d(10.0f, 0.0f, 0.0f), 0.3f, 1.8f));
}
