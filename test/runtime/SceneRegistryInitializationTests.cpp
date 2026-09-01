#include <gtest/gtest.h>
#include <audio/AudioComponentSchemas.h>
#include <render/RenderComponentSchemas.h>

#include <audio/AudioSourceComponent.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/registry/Registry.h>
#include <world/registry/SceneRegistryInitialization.h>
#include <world/transform/TransformComponents.h>

TEST(SceneRegistryInitialization, RegistersManifestStoresAndResources)
{
    Registry registry;
    InitializeSceneRegistry(registry);

    EXPECT_TRUE(registry.Components.IsRegistered<LocalTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<WorldTransform>());
    EXPECT_TRUE(registry.Components.IsRegistered<Parent>());
    EXPECT_TRUE(registry.Components.IsRegistered<StaticMeshComponent>());
    EXPECT_TRUE(registry.Components.IsRegistered<CameraComponent>());
    EXPECT_TRUE(registry.Resources.Has<ActiveCameraService>());
}

// Callers that only inspect structure pass no caches; the component asset
// resources still have to exist or component load hits a missing resource.
TEST(SceneRegistryInitialization, InstallsAssetResourcesWithoutCaches)
{
    Registry registry;
    InitializeSceneRegistry(registry);

    EXPECT_TRUE(registry.Components.HasResource<StaticMeshComponentAssets>());
    EXPECT_TRUE(registry.Components.HasResource<ZoneLightmapComponentAssets>());
    EXPECT_TRUE(registry.Components.HasResource<AudioSourceRuntime>());
}
