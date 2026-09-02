#include <gtest/gtest.h>

#include <anim/AnimationClipPlaybackRuntime.h>
#include <audio/AudioSourceRuntime.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/assets/AssetStoreTable.h>
#include <render/StaticMeshComponent.h>
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

// Callers that only inspect structure pass no caches; the resources the
// systems read through still have to exist so a system finds an empty one
// rather than none.
TEST(SceneRegistryInitialization, InstallsServiceResourcesWithoutCaches)
{
    Registry registry;
    InitializeSceneRegistry(registry);

    EXPECT_TRUE(registry.Components.HasResource<AssetStoreTable>());
    EXPECT_TRUE(registry.Components.HasResource<AudioSourceRuntime>());
    EXPECT_TRUE(registry.Components.HasResource<AnimationClipPlaybackRuntime>());
}
