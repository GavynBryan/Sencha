#include <gtest/gtest.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagation.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/ZoneRuntime.h>

#include <array>

TEST(TransformPropagation, PropagatesDefaultRegistryTransforms)
{
    ZoneRuntime zones;
    Registry& registry = CreateDefault3DZone(
        zones, ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
    EntityId entity = CreateDefaultEntity(registry);
    auto& transforms = registry.Components.Get<TransformStore<Transform3f>>();
    transforms.SetLocal(entity, Transform3f(Vec3d(3.0f, 4.0f, 5.0f), Quatf::Identity(), Vec3d::One()));

    std::array<Registry*, 1> registries{ &registry };
    PropagateTransforms(registries);

    const Transform3f* world = transforms.TryGetWorld(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(TransformPropagation, SkipsRegistriesWithoutTransformResources)
{
    Registry registry;

    std::array<Registry*, 2> registries{ nullptr, &registry };

    EXPECT_NO_THROW(PropagateTransforms(registries));
}
