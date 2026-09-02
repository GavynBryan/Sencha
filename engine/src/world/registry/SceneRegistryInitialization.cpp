#include <world/registry/SceneRegistryInitialization.h>

#include <components/ActiveCameraService.h>
#include <movement/MovementRegistration.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>

#include <utility>

void InitializeSceneRegistry(Registry& registry,
                             AssetStoreTable stores,
                             AudioSourceRuntime audio,
                             AnimationClipPlaybackRuntime animation)
{
    registry.Resources.Register<ActiveCameraService>();
    // The same feature registrars the runtime composes its sealed vocabulary
    // from, aimed at this registry's World instead. An editor preview that knew
    // a different set of components than the runtime would render a different
    // scene than the game does.
    ComponentRegistrar components(registry.Components);
    RegisterEngineComponents(components);
    registry.Components.SetResource(std::move(stores));
    registry.Components.SetResource(audio);
    registry.Components.SetResource(animation);
    // Tags, attributes, abilities, and locomotion modes are named in content by
    // the name they were registered under, so a registry that is going to hold
    // a loaded scene needs the vocabulary those names resolve against -- the
    // engine's own here, and a game module's when one is loaded.
    RegisterMovement(registry.Components);
}
