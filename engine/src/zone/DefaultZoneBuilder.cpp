#include <zone/DefaultZoneBuilder.h>

#include <world/ComponentManifest.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneRuntime.h>

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation,
                              StaticMeshCache* meshes,
                              MaterialSetCache* materialSets,
                              AudioClipCache* audioClips,
                              AudioService* audio,
                              CaptionRuntime* captions)
{
    Registry& registry = zones.CreateZone(zone);
    InitializeDefault3DRegistry(registry, meshes, materialSets, audioClips, audio, captions);
    zones.SetParticipation(zone, participation);
    return registry;
}

void InitializeDefault3DRegistry(Registry& registry,
                                 StaticMeshCache* meshes,
                                 MaterialSetCache* materialSets,
                                 AudioClipCache* audioClips,
                                 AudioService* audio,
                                 CaptionRuntime* captions)
{
    registry.Resources.Register<ActiveCameraService>();
    // Storage traits, not raw RegisterComponent: LocalTransform's traits also
    // register WorldTransform and Parent.
    ForEachSceneComponent([&](auto tag)
    {
        ComponentStorageTraits<typename decltype(tag)::Type>::Register(registry);
    });
    registry.Components.AddResource<StaticMeshComponentAssets>(meshes, materialSets);
    registry.Components.AddResource<AudioSourceRuntime>(audioClips, audio, captions);
}

EntityId CreateDefaultEntity(Registry& registry, const Transform3f& local)
{
    EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, LocalTransform{ local });
    registry.Components.AddComponent(entity, WorldTransform{ local });
    return entity;
}

bool AddDefaultMeshRenderer(Registry& registry,
                            EntityId entity,
                            StaticMeshHandle mesh,
                            MaterialSetHandle materials)
{
    if (!registry.Components.IsAlive(entity)
        || registry.Components.HasComponent<StaticMeshComponent>(entity))
    {
        return false;
    }

    registry.Components.AddComponent(entity, StaticMeshComponent{
        .Mesh = mesh,
        .Materials = materials,
    });
    return true;
}

bool AddDefaultCamera(Registry& registry,
                      EntityId entity,
                      const CameraComponent& camera,
                      bool makeActive)
{
    if (!registry.Components.IsAlive(entity)
        || registry.Components.HasComponent<CameraComponent>(entity))
    {
        return false;
    }

    registry.Components.AddComponent(entity, camera);
    if (makeActive)
        registry.Resources.Get<ActiveCameraService>().SetActive(entity);
    return true;
}
