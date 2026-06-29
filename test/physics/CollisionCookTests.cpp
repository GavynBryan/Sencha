// End-to-end collision cook: triangles -> pre-baked Jolt blob -> runtime load
// -> static collision -> dynamic body rests on it. The whole brush->collision
// path minus the brush tessellation (covered by the level-cook tests).
//
// Cook-only: BakeCollisionBlob is compiled under SENCHA_ENABLE_COOK.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <cstddef>
#include <cstdint>
#include <vector>

#include <assets/cook/CollisionShapeCook.h>
#include <ecs/World.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsScene.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

// A flat floor quad at y = 0, wound so its normal faces up (+Y).
std::vector<std::byte> BakeFloorBlob()
{
    const std::vector<Vec3d> positions = {
        Vec3d(-50.0f, 0.0f, -50.0f),
        Vec3d(50.0f, 0.0f, -50.0f),
        Vec3d(50.0f, 0.0f, 50.0f),
        Vec3d(-50.0f, 0.0f, 50.0f),
    };
    const std::vector<uint32_t> indices = { 0, 2, 1, 0, 3, 2 };
    return BakeCollisionBlob(positions, indices);
}
} // namespace

TEST(CollisionCook, BlobRoundTripsAndIsRaycastable)
{
    const std::vector<std::byte> blob = BakeFloorBlob();
    ASSERT_FALSE(blob.empty());

    PhysicsWorld world;
    CollisionShapeCache cache;
    world.SetShapeCache(&cache);

    const CollisionShapeHandle floor = cache.LoadBlob(blob);
    ASSERT_TRUE(floor.IsValid());
    EXPECT_TRUE(cache.Has(floor));

    BodyDesc desc;
    desc.MeshShape = floor;
    desc.Motion = BodyMotion::Static;
    desc.Layer = CollisionLayer::Static;
    ASSERT_TRUE(world.AddBody(desc).IsValid());

    PhysicsQueries queries(world);
    const RaycastHit hit = queries.Raycast(Vec3d(0.0f, 5.0f, 0.0f), Vec3d(0.0f, -1.0f, 0.0f), 10.0f);
    ASSERT_TRUE(hit.Hit);
    EXPECT_NEAR(hit.Point.Y, 0.0f, 0.05f);
    EXPECT_NEAR(hit.Normal.Y, 1.0f, 0.05f);
}

TEST(CollisionCook, DynamicBodyRestsOnCookedFloorThroughScene)
{
    const std::vector<std::byte> blob = BakeFloorBlob();
    ASSERT_FALSE(blob.empty());

    PhysicsWorld world;
    CollisionShapeCache cache;
    world.SetShapeCache(&cache);
    const CollisionShapeHandle floorShape = cache.LoadBlob(blob);
    ASSERT_TRUE(floorShape.IsValid());

    World ecs;
    ecs.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(ecs);
    PhysicsScene scene(world);

    // Static floor entity carrying the cooked mesh shape (as a cooked scene would).
    const EntityId floor = ecs.CreateEntity();
    ecs.AddComponent<LocalTransform>(floor, LocalTransform{});
    Collider floorCollider;
    floorCollider.Mesh = floorShape;
    ecs.AddComponent<Collider>(floor, floorCollider);

    // Dynamic ball dropped above it.
    Transform3f ballStart;
    ballStart.Position = Vec3d(0.0f, 5.0f, 0.0f);
    const EntityId ball = ecs.CreateEntity();
    ecs.AddComponent<LocalTransform>(ball, LocalTransform{ ballStart });
    ecs.AddComponent<Collider>(ball, Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(ball, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });

    for (int i = 0; i < 240; ++i)
    {
        scene.SyncToPhysics(ecs);
        world.Step(kFixedDt);
        scene.SyncFromPhysics(ecs);
    }

    const LocalTransform* rest = ecs.TryGet<LocalTransform>(ball);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 0.5f, 0.05f); // floor y 0 + radius 0.5
}

#endif // SENCHA_ENABLE_COOK
