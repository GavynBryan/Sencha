#include <gtest/gtest.h>
#include <zone/DefaultZoneBuilder.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHierarchyService.h>
#include <zone/ZoneRuntime.h>

TEST(DefaultZoneBuilder, CreatesZoneAndRegistersDefaultStoresAndResources)
{
    ZoneRuntime runtime;

    Registry& registry = CreateDefault3DZone(
        runtime, ZoneId{ 7 }, ZoneParticipation{ .Visible = true, .Logic = true });

    EXPECT_EQ(&registry, runtime.FindZone(ZoneId{ 7 }));
    EXPECT_TRUE(registry.Components.IsRegistered<LocalTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<WorldTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<Parent>());
    EXPECT_TRUE(registry.Components.IsRegistered<StaticMeshComponent>());
    EXPECT_TRUE(registry.Components.IsRegistered<CameraComponent>());
    EXPECT_TRUE(registry.Resources.Has<TransformHierarchyService>());
    EXPECT_TRUE(registry.Resources.Has<ActiveCameraService>());
    EXPECT_TRUE(runtime.GetParticipation(ZoneId{ 7 }).Visible);
    EXPECT_TRUE(runtime.GetParticipation(ZoneId{ 7 }).Logic);
}

TEST(DefaultZoneBuilder, CreateEntityAddsTransformAndHierarchyRegistration)
{
    ZoneRuntime runtime;
    Registry& registry = CreateDefault3DZone(runtime, ZoneId{ 1 });

    EntityId entity = CreateDefaultEntity(registry, Transform3f(
        Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d::One()));

    auto& hierarchy = registry.Resources.Get<TransformHierarchyService>();

    EXPECT_TRUE(registry.Entities.IsAlive(entity));
    EXPECT_TRUE(registry.Components.HasComponent<LocalTransform>(entity));
    EXPECT_TRUE(registry.Components.HasComponent<WorldTransform>(entity));
    EXPECT_TRUE(hierarchy.IsRegistered(entity));
}

TEST(DefaultZoneBuilder, AddCameraCanSetActiveCamera)
{
    ZoneRuntime runtime;
    Registry& registry = CreateDefault3DZone(runtime, ZoneId{ 1 });
    EntityId camera = CreateDefaultEntity(registry);

    const bool added = AddDefaultCamera(registry, camera, CameraComponent{}, true);

    EXPECT_TRUE(added);
    EXPECT_EQ(registry.Resources.Get<ActiveCameraService>().GetActive(), camera);
}
