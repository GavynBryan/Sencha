#include <zone/DefaultZoneBuilder.h>

#include <world/registry/Registry.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>
#include <zone/ZoneRuntime.h>

Registry& CreateDefault3DZone(ZoneRuntime& zones,
                              ZoneId zone,
                              ZoneParticipation participation,
                              StaticMeshCache* meshes,
                              MaterialCache* materials)
{
    Registry& registry = zones.CreateZone(zone);
    auto& order = registry.Resources.Register<TransformPropagationOrderService>();
    registry.Resources.Register<TransformHierarchyService>();
    registry.Resources.Register<ActiveCameraService>();
    registry.Components.Register<TransformStore<Transform3f>>(order);
    if (meshes != nullptr && materials != nullptr)
        registry.Components.Register<MeshRendererStore>(*meshes, *materials);
    else
        registry.Components.Register<MeshRendererStore>();
    registry.Components.Register<CameraStore>();
    zones.SetParticipation(zone, participation);
    return registry;
}

EntityId CreateDefaultEntity(Registry& registry, const Transform3f& local)
{
    EntityId entity = registry.Entities.Create();
    registry.Resources.Get<TransformHierarchyService>().Register(entity);
    registry.Components.Get<TransformStore<Transform3f>>().Add(entity, local);
    return entity;
}

bool AddDefaultMeshRenderer(Registry& registry,
                            EntityId entity,
                            StaticMeshHandle mesh,
                            MaterialHandle material)
{
    return registry.Components.Get<MeshRendererStore>().Add(entity, MeshRendererComponent{
        .Mesh = mesh,
        .Material = material,
    });
}

bool AddDefaultCamera(Registry& registry,
                      EntityId entity,
                      const CameraComponent& camera,
                      bool makeActive)
{
    const bool added = registry.Components.Get<CameraStore>().Add(entity, camera);
    if (added && makeActive)
        registry.Resources.Get<ActiveCameraService>().SetActive(entity);
    return added;
}
