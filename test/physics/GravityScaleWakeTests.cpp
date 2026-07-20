#include <gtest/gtest.h>

#include <ecs/World.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <utility>

TEST(RigidBodyBinding, GravityEditWakesSleepingBody)
{
    constexpr float dt = 1.0f / 60.0f;

    PhysicsWorld physics;
    World world;
    world.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(world);
    RigidBodyBinding binding(physics);

    Transform3f transform;
    transform.Position = Vec3d(0.0f, 5.0f, 0.0f);

    const EntityId entity = world.CreateEntity();
    world.AddComponent<LocalTransform>(entity, LocalTransform{ transform });
    world.AddComponent<Collider>(
        entity, Collider{ CollisionShape::MakeSphere(0.5f) });

    RigidBody body;
    body.Motion = BodyMotion::Dynamic;
    body.GravityScale = 0.0f;
    world.AddComponent<RigidBody>(entity, body);

    binding.SyncToPhysics(world);
    const PhysicsBodyLink* link = std::as_const(world).TryGet<PhysicsBodyLink>(entity);
    ASSERT_NE(link, nullptr);

    // A motionless zero-gravity body should naturally enter the sleep state.
    for (int i = 0; i < 600 && physics.IsBodyActive(link->Body); ++i)
    {
        physics.Step(dt);
        binding.SyncFromPhysics(world);
    }
    ASSERT_FALSE(physics.IsBodyActive(link->Body));

    RigidBody* edited = world.TryGet<RigidBody>(entity);
    ASSERT_NE(edited, nullptr);
    edited->GravityScale = 1.0f;

    binding.SyncToPhysics(world);
    EXPECT_TRUE(physics.IsBodyActive(link->Body));

    physics.Step(dt);
    binding.SyncFromPhysics(world);
    EXPECT_LT(std::as_const(world).TryGet<RigidBody>(entity)->LinearVelocity.Y, 0.0f);
}
