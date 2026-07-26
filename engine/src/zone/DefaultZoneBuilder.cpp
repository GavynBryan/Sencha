#include <zone/DefaultZoneBuilder.h>

#include <world/ComponentManifest.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/transform/TransformComponents.h>

void InitializeDefault3DRegistry(
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

EntityId CreateDefaultEntity(
    Registry& registry,
    const Transform3f& local)
{
    const EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(
        entity,
        LocalTransform{ local });
    registry.Components.AddComponent(
        entity,
        WorldTransform{ local });
    return entity;
}

bool AddDefaultMeshRenderer(
    Registry& registry,
    EntityId entity,
    StaticMeshHandle mesh,
    MaterialSetHandle materials)
{
    if (!registry.Components.IsAlive(entity)
        || registry.Components.HasComponent<StaticMeshComponent>(entity))
    {
        return false;
    }

    registry.Components.AddComponent(
        entity,
        StaticMeshComponent{
            .Mesh = mesh,
            .Materials = materials,
        });
    return true;
}

bool AddDefaultCamera(
    Registry& registry,
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
    {
        registry.Resources
            .Get<ActiveCameraService>()
            .SetActive(entity);
    }
    return true;
}
