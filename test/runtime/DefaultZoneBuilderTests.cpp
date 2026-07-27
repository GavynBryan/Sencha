#include <gtest/gtest.h>

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <zone/DefaultZoneBuilder.h>

TEST(DefaultZoneBuilder, InitializesEditorRegistryStoresAndResources)
{
    Registry registry;
    InitializeDefault3DRegistry(registry);

    EXPECT_TRUE(registry.Components.IsRegistered<LocalTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<WorldTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<Parent>());
    EXPECT_TRUE(registry.Components.IsRegistered<StaticMeshComponent>());
    EXPECT_TRUE(registry.Components.IsRegistered<CameraComponent>());
    EXPECT_TRUE(registry.Resources.Has<ActiveCameraService>());
}

TEST(DefaultZoneBuilder, CreateEntityAddsTransformComponents)
{
    Registry registry;
    InitializeDefault3DRegistry(registry);

    const EntityId entity = CreateDefaultEntity(
        registry,
        Transform3f(
            Vec3d(1.0f, 2.0f, 3.0f),
            Quatf::Identity(),
            Vec3d::One()));

    EXPECT_TRUE(registry.Components.IsAlive(entity));
    EXPECT_TRUE(
        registry.Components.HasComponent<LocalTransform>(entity));
    EXPECT_TRUE(
        registry.Components.HasComponent<WorldTransform>(entity));
}

TEST(DefaultZoneBuilder, AddCameraCanSetEditorActiveCamera)
{
    Registry registry;
    InitializeDefault3DRegistry(registry);
    const EntityId camera = CreateDefaultEntity(registry);

    const bool added = AddDefaultCamera(
        registry,
        camera,
        CameraComponent{},
        true);

    EXPECT_TRUE(added);
    EXPECT_EQ(
        registry.Resources
            .Get<ActiveCameraService>()
            .GetActive(),
        camera);
}
