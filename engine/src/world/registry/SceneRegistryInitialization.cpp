#include <world/registry/SceneRegistryInitialization.h>

#include <audio/AudioSourceComponent.h>
#include <components/ActiveCameraService.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/ComponentManifest.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>

void InitializeSceneRegistry(
    Registry& registry,
    StaticMeshCache* meshes,
    MaterialSetCache* materialSets,
    AudioClipCache* audioClips,
    AudioService* audio,
    CaptionRuntime* captions,
    TextureCache* textures)
{
    registry.Resources.Register<ActiveCameraService>();
    ForEachSceneComponent([&](auto tag)
    {
        ComponentStorageTraits<
            typename decltype(tag)::Type>::Register(registry);
    });
    registry.Components.AddResource<StaticMeshComponentAssets>(
        meshes,
        materialSets);
    registry.Components.AddResource<ZoneLightmapComponentAssets>(textures);
    registry.Components.AddResource<AudioSourceRuntime>(
        audioClips,
        audio,
        captions);
}
