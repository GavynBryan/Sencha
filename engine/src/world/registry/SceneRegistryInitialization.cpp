#include <world/registry/SceneRegistryInitialization.h>

#include <audio/AudioSourceComponent.h>
#include <components/ActiveCameraService.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>

void InitializeSceneRegistry(
    Registry& registry,
    StaticMeshCache* meshes,
    MaterialSetCache* materialSets,
    AudioClipCache* audioClips,
    AudioService* audio,
    CaptionRuntime* captions,
    TextureCache* textures,
    SkinnedMeshCache* skinnedMeshes)
{
    registry.Resources.Register<ActiveCameraService>();
    // The same feature registrars the runtime composes its sealed vocabulary
    // from, aimed at this registry's World instead. An editor preview that knew
    // a different set of components than the runtime would render a different
    // scene than the game does.
    ComponentRegistrar components(registry.Components);
    RegisterEngineComponents(components);
    registry.Components.AddResource<StaticMeshComponentAssets>(
        meshes,
        materialSets);
    registry.Components.AddResource<SkinnedMeshComponentAssets>(
        skinnedMeshes,
        materialSets);
    registry.Components.AddResource<ZoneLightmapComponentAssets>(textures);
    registry.Components.AddResource<AudioSourceRuntime>(
        audioClips,
        audio,
        captions);
}
