#include <zone/DefaultZoneBuilder.h>

#include <world/registry/Registry.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneRuntime.h>

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation,
                              StaticMeshCache* meshes,
                              MaterialCache* materials)
{
    Registry& registry = zones.CreateZone(zone);
    registry.Resources.Register<TransformHierarchyService>();
    registry.Resources.Register<ActiveCameraService>();
    registry.Components.RegisterComponent<LocalTransform>();
    registry.Components.RegisterComponent<WorldTransform>();
    registry.Components.RegisterComponent<Parent>();
    registry.Components.RegisterComponent<StaticMeshComponent>();
    registry.Components.RegisterComponent<CameraComponent>();
    registry.Components.AddResource<StaticMeshComponentAssets>(meshes, materials);
    zones.SetParticipation(zone, participation);
    return registry;
}

EntityId CreateDefaultEntity(Registry& registry, const Transform3f& local)
{
    EntityId entity = registry.Components.CreateEntity();
    registry.Resources.Get<TransformHierarchyService>().Register(entity);
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
