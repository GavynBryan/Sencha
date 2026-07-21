#include <gtest/gtest.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

namespace
{
constexpr StoragePartitionId kFirst{ 1 };
constexpr StoragePartitionId kSecond{ 2 };

void RegisterTransforms(World& world)
{
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
}

EntityId CreateTransformEntity(
    World& world,
    StoragePartitionId partition,
    const Vec3d& localPosition)
{
    Transform3f local;
    local.Position = localPosition;

    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ local });
    world.AddComponent<WorldTransform>(
        entity,
        WorldTransform{});
    return entity;
}

StoragePartitionSet Partitions(
    bool first,
    bool second)
{
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    if (first)
        partitions.Add(kFirst);
    if (second)
        partitions.Add(kSecond);
    return partitions;
}
} // namespace

TEST(PartitionTransformPropagation, VisitsOnlyActivePartitionChunks)
{
    World world;
    RegisterTransforms(world);

    const EntityId first = CreateTransformEntity(
        world,
        kFirst,
        Vec3d(1.0f, 2.0f, 3.0f));
    const EntityId second = CreateTransformEntity(
        world,
        kSecond,
        Vec3d(4.0f, 5.0f, 6.0f));

    const StoragePartitionSet active = Partitions(true, false);
    PropagateTransforms(
        world,
        active,
        TransformPropagationDomain::Simulation);

    EXPECT_EQ(
        world.TryGet<WorldTransform>(first)->Value.Position,
        Vec3d(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(
        world.TryGet<WorldTransform>(second)->Value.Position,
        Vec3d::Zero());
}

TEST(PartitionTransformPropagation, CrossPartitionParentChainResolvesExactly)
{
    World world;
    RegisterTransforms(world);

    const EntityId root = CreateTransformEntity(
        world,
        kFirst,
        Vec3d(100.0f, 1.0f, 0.0f));
    const EntityId child = CreateTransformEntity(
        world,
        kSecond,
        Vec3d(0.0f, 2.0f, 0.0f));
    const EntityId grandchild = CreateTransformEntity(
        world,
        kSecond,
        Vec3d(0.0f, 0.0f, 3.0f));
    world.AddComponent<Parent>(child, Parent{ root });
    world.AddComponent<Parent>(grandchild, Parent{ child });

    const StoragePartitionSet active = Partitions(true, true);
    PropagateTransforms(
        world,
        active,
        TransformPropagationDomain::Simulation);

    EXPECT_EQ(
        world.TryGet<WorldTransform>(root)->Value.Position,
        Vec3d(100.0f, 1.0f, 0.0f));
    EXPECT_EQ(
        world.TryGet<WorldTransform>(child)->Value.Position,
        Vec3d(100.0f, 3.0f, 0.0f));
    EXPECT_EQ(
        world.TryGet<WorldTransform>(grandchild)->Value.Position,
        Vec3d(100.0f, 3.0f, 3.0f));
}

TEST(PartitionTransformPropagation, DormantPartitionSkipsWorkAndCatchesUpOnReentry)
{
    World world;
    RegisterTransforms(world);

    const EntityId root = CreateTransformEntity(
        world,
        kFirst,
        Vec3d(10.0f, 0.0f, 0.0f));
    const EntityId child = CreateTransformEntity(
        world,
        kSecond,
        Vec3d(0.0f, 2.0f, 0.0f));
    world.AddComponent<Parent>(child, Parent{ root });

    const StoragePartitionSet both = Partitions(true, true);
    PropagateTransforms(
        world,
        both,
        TransformPropagationDomain::Simulation);
    ASSERT_EQ(
        world.TryGet<WorldTransform>(child)->Value.Position,
        Vec3d(10.0f, 2.0f, 0.0f));

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(root)->Value.Position.X = 25.0f;

    const StoragePartitionSet firstOnly = Partitions(true, false);
    PropagateTransforms(
        world,
        firstOnly,
        TransformPropagationDomain::Simulation);

    EXPECT_EQ(
        world.TryGet<WorldTransform>(root)->Value.Position,
        Vec3d(25.0f, 0.0f, 0.0f));
    EXPECT_EQ(
        world.TryGet<WorldTransform>(child)->Value.Position,
        Vec3d(10.0f, 2.0f, 0.0f));

    world.AdvanceFrame();
    PropagateTransforms(
        world,
        both,
        TransformPropagationDomain::Simulation);

    EXPECT_EQ(
        world.TryGet<WorldTransform>(child)->Value.Position,
        Vec3d(25.0f, 2.0f, 0.0f));
}

TEST(PartitionTransformPropagation, SimulationAndPresentationKeepIndependentActivityHistory)
{
    World world;
    RegisterTransforms(world);

    const EntityId entity = CreateTransformEntity(
        world,
        kSecond,
        Vec3d(1.0f, 0.0f, 0.0f));
    const StoragePartitionSet visible = Partitions(false, true);
    const StoragePartitionSet logic = Partitions(false, false);

    PropagateTransforms(
        world,
        visible,
        TransformPropagationDomain::Presentation);
    ASSERT_EQ(
        world.TryGet<WorldTransform>(entity)->Value.Position,
        Vec3d(1.0f, 0.0f, 0.0f));

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(entity)->Value.Position.X = 3.0f;
    PropagateTransforms(
        world,
        logic,
        TransformPropagationDomain::Simulation);
    EXPECT_EQ(
        world.TryGet<WorldTransform>(entity)->Value.Position,
        Vec3d(1.0f, 0.0f, 0.0f));

    PropagateTransforms(
        world,
        visible,
        TransformPropagationDomain::Presentation);
    EXPECT_EQ(
        world.TryGet<WorldTransform>(entity)->Value.Position,
        Vec3d(3.0f, 0.0f, 0.0f));
}

TEST(PartitionTransformPropagation, EntityMigrationInvalidatesCachedRowPointers)
{
    World world;
    RegisterTransforms(world);

    const EntityId entity = CreateTransformEntity(
        world,
        kFirst,
        Vec3d(2.0f, 0.0f, 0.0f));
    const StoragePartitionSet both = Partitions(true, true);

    PropagateTransforms(
        world,
        both,
        TransformPropagationDomain::Simulation);
    ASSERT_TRUE(world.MoveEntityToPartition(entity, kSecond));

    world.AdvanceFrame();
    world.TryGet<LocalTransform>(entity)->Value.Position.X = 7.0f;
    PropagateTransforms(
        world,
        both,
        TransformPropagationDomain::Simulation);

    EXPECT_TRUE(world.IsAlive(entity));
    EXPECT_EQ(world.GetEntityPartition(entity), kSecond);
    EXPECT_EQ(
        world.TryGet<WorldTransform>(entity)->Value.Position,
        Vec3d(7.0f, 0.0f, 0.0f));
}
