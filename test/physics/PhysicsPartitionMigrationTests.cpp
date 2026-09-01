#include <ecs/StoragePartitionSet.h>
#include <world/WorldComponentSchemas.h>
#include <ecs/World.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

namespace
{
void RegisterPhysicsWorld(World& world)
{
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    RegisterPhysicsComponents(world);
}

StoragePartitionSet Active(
    StoragePartitionId first,
    StoragePartitionId second)
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    partitions.Add(first);
    partitions.Add(second);
    return partitions;
}

EntityId SpawnProjectile(
    World& world,
    StoragePartitionId partition)
{
    Transform3f transform;
    transform.Position = Vec3d(0.0f, 5.0f, 0.0f);

    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    world.AddComponent<WorldTransform>(
        entity,
        WorldTransform{ transform });
    world.AddComponent<Collider>(
        entity,
        Collider{ CollisionShape::MakeSphere(0.25f) });

    RigidBody body;
    body.Motion = BodyMotion::Dynamic;
    body.LinearVelocity = Vec3d(4.0f, 0.0f, 0.0f);
    body.GravityScale = 0.0f;
    world.AddComponent<RigidBody>(entity, body);
    return entity;
}
} // namespace

TEST(PhysicsPartitionMigration, ActiveToActiveKeepsBackendBodyAndEntityIdentity)
{
    constexpr StoragePartitionId first{ 1 };
    constexpr StoragePartitionId second{ 2 };

    PhysicsWorld physics;
    World world;
    RegisterPhysicsWorld(world);
    RigidBodyBinding binding(physics);
    const StoragePartitionSet active = Active(first, second);

    const EntityId projectile = SpawnProjectile(world, first);
    binding.SyncToPhysics(world, active);

    const PhysicsBodyLink* before =
        world.TryGet<PhysicsBodyLink>(projectile);
    ASSERT_NE(before, nullptr);
    const PhysicsBodyId originalBody = before->Body;
    ASSERT_TRUE(originalBody.IsValid());

    ASSERT_TRUE(world.MoveEntityToPartition(projectile, second));
    binding.SyncToPhysics(world, active);

    const PhysicsBodyLink* after =
        world.TryGet<PhysicsBodyLink>(projectile);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->Body, originalBody);
    EXPECT_EQ(binding.BodyCount(), 1u);
    EXPECT_EQ(physics.BodyCount(), 1u);
    EXPECT_TRUE(world.IsAlive(projectile));
    EXPECT_EQ(world.GetEntityPartition(projectile), second);

    physics.Step(1.0f / 60.0f);
    binding.SyncFromPhysics(world, active);
    EXPECT_GT(
        world.TryGet<LocalTransform>(projectile)
            ->Value.Position.X,
        0.0f);
}

TEST(PhysicsPartitionMigration, MovingIntoDormantPartitionEvictsThenRestores)
{
    constexpr StoragePartitionId first{ 1 };
    constexpr StoragePartitionId dormant{ 2 };

    PhysicsWorld physics;
    World world;
    RegisterPhysicsWorld(world);
    RigidBodyBinding binding(physics);

    StoragePartitionSet active;
    active.Add(StoragePartitionId::Default());
    active.Add(first);

    const EntityId projectile = SpawnProjectile(world, first);
    binding.SyncToPhysics(world, active);
    ASSERT_TRUE(world.HasComponent<PhysicsBodyLink>(projectile));

    ASSERT_TRUE(world.MoveEntityToPartition(projectile, dormant));
    binding.SyncToPhysics(world, active);

    EXPECT_TRUE(world.IsAlive(projectile));
    EXPECT_EQ(world.GetEntityPartition(projectile), dormant);
    EXPECT_FALSE(world.HasComponent<PhysicsBodyLink>(projectile));
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_FLOAT_EQ(
        world.TryGet<RigidBody>(projectile)->LinearVelocity.X,
        4.0f);

    active.Add(dormant);
    binding.SyncToPhysics(world, active);

    EXPECT_TRUE(world.HasComponent<PhysicsBodyLink>(projectile));
    EXPECT_EQ(binding.BodyCount(), 1u);
    EXPECT_EQ(physics.BodyCount(), 1u);
    EXPECT_FLOAT_EQ(
        physics.GetLinearVelocity(
            world.TryGet<PhysicsBodyLink>(projectile)->Body).X,
        4.0f);
}
