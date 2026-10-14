#include <world/scene/SceneInstanceGroup.h>

#include <ecs/World.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/transform/TransformComponents.h>

#include <vector>

EntityId SingleRoot(const World& world, std::span<const EntityId> members)
{
    EntityId root;
    for (const EntityId member : members)
    {
        if (!world.IsAlive(member) || world.TryGet<Parent>(member) != nullptr)
            continue;
        if (root.IsValid())
            return EntityId{};
        root = member;
    }
    return root;
}

void DestroySceneInstance(World& world, SceneInstanceId instance)
{
    const auto* index = world.TryGetResource<SceneInstanceIndex>();
    if (index == nullptr)
        return;

    // Destruction prunes the index through the component hooks, so walk a copy.
    const std::span<const EntityId> members = index->Entities(instance);
    const std::vector<EntityId> snapshot(members.begin(), members.end());
    for (const EntityId entity : snapshot)
        if (world.IsAlive(entity))
            world.DestroyEntity(entity);
}
