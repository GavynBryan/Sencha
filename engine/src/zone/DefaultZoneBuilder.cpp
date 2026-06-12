#include <zone/DefaultZoneBuilder.h>

#include <audio/AudioSourceComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneRuntime.h>

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation,
                              StaticMeshCache* meshes,
                              MaterialCache* materials,
                              AudioClipCache* audioClips,
                              AudioService* audio)
{
    Registry& registry = zones.CreateZone(zone);
    InitializeDefault3DRegistry(registry, meshes, materials, audioClips, audio);
    zones.SetParticipation(zone, participation);
    return registry;
}

void InitializeDefault3DRegistry(Registry& registry,
                                 StaticMeshCache* meshes,
                                 MaterialCache* materials,
                                 AudioClipCache* audioClips,
                                 AudioService* audio)
{
    registry.Resources.Register<ActiveCameraService>();
    registry.Components.RegisterComponent<LocalTransform>();
    registry.Components.RegisterComponent<WorldTransform>();
    registry.Components.RegisterComponent<Parent>();
    registry.Components.RegisterComponent<StaticMeshComponent>();
    registry.Components.RegisterComponent<CameraComponent>();
    registry.Components.RegisterComponent<AudioSourceComponent>();
    registry.Components.AddResource<StaticMeshComponentAssets>(meshes, materials);
    registry.Components.AddResource<AudioSourceRuntime>(audioClips, audio);
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
                            MaterialHandle material)
{
    if (!registry.Components.IsAlive(entity)
        || registry.Components.HasComponent<StaticMeshComponent>(entity))
    {
        return false;
    }

    registry.Components.AddComponent(entity, StaticMeshComponent{
        .Mesh = mesh,
        .Material = material,
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
