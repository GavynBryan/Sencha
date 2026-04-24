#include <gtest/gtest.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformPropagation.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/ZoneRuntime.h>

#include <array>

TEST(TransformPropagation, PropagatesDefaultRegistryTransforms)
{
    ZoneRuntime zones;
    Registry& registry = CreateDefault3DZone(
        zones, ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
    EntityId entity = CreateDefaultEntity(registry);
    registry.Components.TryGet<LocalTransform>(entity)->Value =
        Transform3f(Vec3d(3.0f, 4.0f, 5.0f), Quatf::Identity(), Vec3d::One());

    std::array<Registry*, 1> registries{ &registry };
    PropagateTransforms(registries);

    const WorldTransform* world = registry.Components.TryGet<WorldTransform>(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->Value.Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(TransformPropagation, SkipsRegistriesWithoutTransformResources)
{
    Registry registry;

    std::array<Registry*, 2> registries{ nullptr, &registry };

    EXPECT_NO_THROW(PropagateTransforms(registries));
}

TEST(TransformPropagation, RootWorldEqualsLocalInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId entity = world.CreateEntity();
    const Transform3f local(Vec3d(3.0f, 4.0f, 5.0f), Quatf::Identity(), Vec3d::One());
    world.AddComponent(entity, LocalTransform{ local });
    world.AddComponent(entity, WorldTransform{});

    PropagateTransforms(world);

    const WorldTransform* worldTransform = world.TryGet<WorldTransform>(entity);
    ASSERT_NE(worldTransform, nullptr);
    EXPECT_EQ(worldTransform->Value.Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(TransformPropagation, ChildInheritsParentInEcsWorld)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId parent = world.CreateEntity();
    EntityId child = world.CreateEntity();

    const Transform3f parentLocal(Vec3d(10.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One());
    const Transform3f childLocal(Vec3d(2.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One());

    world.AddComponent(parent, LocalTransform{ parentLocal });
    world.AddComponent(parent, WorldTransform{});
    world.AddComponent(child, LocalTransform{ childLocal });
    world.AddComponent(child, WorldTransform{});
    world.AddComponent(child, Parent{ parent });

    PropagateTransforms(world);

    const WorldTransform* childWorld = world.TryGet<WorldTransform>(child);
    ASSERT_NE(childWorld, nullptr);
    EXPECT_EQ(childWorld->Value.Position, Vec3d(12.0f, 0.0f, 0.0f));
}

TEST(TransformPropagation, EcsPropagationBumpsWorldTransformChangedChunks)
{
    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    EntityId entity = world.CreateEntity();
    world.AddComponent(entity, LocalTransform{ Transform3f(Vec3d(1.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One()) });
    world.AddComponent(entity, WorldTransform{});

    world.AdvanceFrame();
    PropagateTransforms(world);

    uint32_t changedRows = 0;
    Query<Read<WorldTransform>, Changed<WorldTransform>> changed(world);
    changed.ForEachChunk([&](auto& view)
    {
        changedRows += view.Count();
    }, 0);

    EXPECT_EQ(changedRows, 1u);
}
