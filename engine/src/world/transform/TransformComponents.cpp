#include <world/transform/TransformComponents.h>

#include <ecs/World.h>
#include <world/registry/Registry.h>

void ComponentStorageTraits<LocalTransform>::Register(World& world)
{
    if (!world.IsRegistered<LocalTransform>())
        world.RegisterComponent<LocalTransform>();
    if (!world.IsRegistered<WorldTransform>())
        world.RegisterComponent<WorldTransform>();
    if (!world.IsRegistered<Parent>())
        world.RegisterComponent<Parent>();
}

void ComponentStorageTraits<LocalTransform>::Register(Registry& registry)
{
    Register(registry.Components);
}

bool ComponentStorageTraits<LocalTransform>::Add(World& world, EntityId entity,
                                                 LocalTransform component)
{
    if (world.HasComponent<LocalTransform>(entity))
        return false;

    world.AddComponent(entity, component);
    if (!world.HasComponent<WorldTransform>(entity))
        world.AddComponent(entity, WorldTransform{ component.Value });
    return true;
}

bool ComponentStorageTraits<LocalTransform>::Add(Registry& registry, EntityId entity,
                                                 LocalTransform component)
{
    return Add(registry.Components, entity, component);
}
