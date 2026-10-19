#include "AllocationCounter.h"

#include <world/transform/TransformPropagation.h>

#include <gtest/gtest.h>

TEST(TransformPropagationAllocation, RepeatedSimulationAndPresentationReuseQueries)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();
    world.RegisterComponent<WorldTransformHistory>();
    const auto parent = world.CreateEntity();
    world.AddComponent<LocalTransform>(parent, LocalTransform{});
    world.AddComponent<WorldTransform>(parent, WorldTransform{});
    const auto child = world.CreateEntity();
    world.AddComponent<LocalTransform>(child, LocalTransform{});
    world.AddComponent<WorldTransform>(child, WorldTransform{});
    world.AddComponent<Parent>(child, Parent{ parent });
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    const auto propagate = [&] {
        PropagateTransforms(world, partitions, TransformPropagationDomain::Simulation);
        PropagateTransforms(world, partitions, TransformPropagationDomain::Presentation);
    };
    propagate();
    for (int i = 0; i < 4; ++i)
    {
        world.AdvanceFrame();
        propagate();
    }
    const auto before = AllocationCount();
    for (int i = 0; i < 100; ++i)
    {
        world.AdvanceFrame();
        world.TryGet<LocalTransform>(parent)->Value.Position.X += 1;
        propagate();
    }
    const auto after = AllocationCount();
    EXPECT_EQ(after, before);
    EXPECT_FLOAT_EQ(std::as_const(world).TryGet<WorldTransform>(child)->Value.Position.X, 100);
}
