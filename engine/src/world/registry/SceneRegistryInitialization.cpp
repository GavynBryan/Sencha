#include <world/registry/SceneRegistryInitialization.h>
#include <anim/AnimationComponentSchemas.h>
#include <audio/AudioComponentSchemas.h>

#include <anim/AnimationClipPlayerComponent.h>
#include <audio/AudioSourceComponent.h>
#include <components/ActiveCameraService.h>
#include <movement/MovementComponentSchemas.h>
#include <movement/MovementRegistration.h>
#include <render/StaticMeshComponent.h>
#include <render/RenderComponentSchemas.h>
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
    SkinnedMeshCache* skinnedMeshes,
    AnimationClipCache* clips)
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
    registry.Components.AddResource<AnimationClipComponentAssets>(clips);
    registry.Components.AddResource<ZoneLightmapComponentAssets>(textures);
    registry.Components.AddResource<AudioSourceRuntime>(
        audioClips,
        audio,
        captions);
    // Tags, attributes, abilities, and locomotion modes are named in content by
    // the name they were registered under, so a registry that is going to hold
    // a loaded scene needs the vocabulary those names resolve against -- the
    // engine's own here, and a game module's when one is loaded.
    RegisterMovement(registry.Components);
}
