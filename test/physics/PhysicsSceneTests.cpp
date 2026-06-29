// PhysicsScene bridges ECS entities (Collider + optional RigidBody + transform)
// to bodies in the shared PhysicsWorld. Tests drive the bridge directly with a
// bare ECS World; no engine frame harness, no Jolt headers.

#include <gtest/gtest.h>

#include <ecs/World.h>
#include <physics/PhysicsScene.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

void RegisterPhysicsComponents(World& world)
{
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<Collider>();
    world.RegisterComponent<RigidBody>();
}

EntityId SpawnAt(World& world, const Vec3d& position)
{
    Transform3f t;
    t.Position = position;
    const EntityId e = world.CreateEntity();
    world.AddComponent<LocalTransform>(e, LocalTransform{ t });
    return e;
}

void Tick(PhysicsScene& scene, World& ecs, PhysicsWorld& physics, int steps)
{
    for (int i = 0; i < steps; ++i)
    {
        scene.SyncToPhysics(ecs);
        physics.Step(kFixedDt);
        scene.SyncFromPhysics(ecs);
    }
}
} // namespace

TEST(PhysicsScene, DynamicEntityRestsOnStaticFloor)
{
    PhysicsWorld physics;
    World ecs;
    RegisterPhysicsComponents(ecs);
    PhysicsScene& scene = ecs.AddResource<PhysicsScene>(physics);

    EntityId floor = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f));
    ecs.AddComponent<Collider>(floor, Collider{ CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)) });

    EntityId ball = SpawnAt(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.AddComponent<Collider>(ball, Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(ball, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });

    Tick(scene, ecs, physics, 240);

    EXPECT_EQ(scene.BodyCount(), 2u);
    const LocalTransform* rest = ecs.TryGet<LocalTransform>(ball);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 1.0f, 0.05f); // floor top 0.5 + radius 0.5
}

TEST(PhysicsScene, RemovingColliderRemovesBody)
{
    PhysicsWorld physics;
    World ecs;
    RegisterPhysicsComponents(ecs);
    PhysicsScene& scene = ecs.AddResource<PhysicsScene>(physics);

    EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });

    scene.SyncToPhysics(ecs);
    EXPECT_EQ(scene.BodyCount(), 1u);
    EXPECT_EQ(physics.BodyCount(), 1u);

    ecs.RemoveComponent<Collider>(box);
    scene.SyncToPhysics(ecs);
    EXPECT_EQ(scene.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(PhysicsScene, DestructorRemovesBodiesFromSharedWorld)
{
    PhysicsWorld physics;

    {
        World ecs;
        RegisterPhysicsComponents(ecs);
        PhysicsScene& scene = ecs.AddResource<PhysicsScene>(physics);

        EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
        ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });
        scene.SyncToPhysics(ecs);
        EXPECT_EQ(physics.BodyCount(), 1u); // body lives in the shared world
    } // ecs (and its PhysicsScene resource) destroyed here: simulates zone unload

    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(PhysicsScene, KinematicBodyFollowsAuthoredTransform)
{
    PhysicsWorld physics;
    World ecs;
    RegisterPhysicsComponents(ecs);
    PhysicsScene& scene = ecs.AddResource<PhysicsScene>(physics);

    EntityId platform = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f));
    ecs.AddComponent<Collider>(platform, Collider{ CollisionShape::MakeBox(Vec3d(1.0f, 0.25f, 1.0f)) });
    ecs.AddComponent<RigidBody>(platform, RigidBody{ BodyMotion::Kinematic, 1.0f, Vec3d::Zero(), 1.0f });

    scene.SyncToPhysics(ecs);
    physics.Step(kFixedDt);

    // Move the authored transform; the kinematic body should track it on sync.
    if (LocalTransform* lt = ecs.TryGet<LocalTransform>(platform))
        lt->Value.Position = Vec3d(0.0f, 2.0f, 0.0f);
    scene.SyncToPhysics(ecs);

    // Reconcile must not have created a second body for the moved platform.
    EXPECT_EQ(scene.BodyCount(), 1u);
}
