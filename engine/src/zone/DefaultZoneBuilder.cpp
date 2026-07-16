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
    registry.Resources.Register<StaticMeshComponentAssets>(meshes, materialSets);
    registry.Resources.Register<AudioSourceRuntime>(audioClips, audio, captions);
    // Storage traits, not raw RegisterComponent: LocalTransform's traits also
    // register WorldTransform and Parent.
    ForEachSceneComponent([&](auto tag)
    {
        ComponentStorageTraits<typename decltype(tag)::Type>::Register(registry);
    });
}

EntityId CreateDefaultEntity(Registry& registry, const Transform3f& local)
{
    EntityId entity = registry.Entities.CreateEntity();
    registry.Entities.AddComponent(entity, LocalTransform{ local });
    registry.Entities.AddComponent(entity, WorldTransform{ local });
    return entity;
}

bool AddDefaultMeshRenderer(Registry& registry,
                            EntityId entity,
                            StaticMeshHandle mesh,
                            MaterialSetHandle materials)
{
    if (!registry.Entities.IsAlive(entity)
        || registry.Entities.HasComponent<StaticMeshComponent>(entity))
    {
        return false;
    }

    registry.Entities.AddComponent(entity, StaticMeshComponent{
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
    if (!registry.Entities.IsAlive(entity)
        || registry.Entities.HasComponent<CameraComponent>(entity))
    {
        return false;
    }

    registry.Entities.AddComponent(entity, camera);
    if (makeActive)
        registry.Resources.Get<ActiveCameraService>().SetActive(entity);
    return true;
}
