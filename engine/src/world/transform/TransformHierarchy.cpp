#include <world/transform/TransformHierarchy.h>

#include <ecs/World.h>
#include <world/transform/TransformComponents.h>

#include <unordered_map>

namespace
{
    using ChildrenOf = std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash>;

    void AppendSubtree(const ChildrenOf& children, EntityId entity, std::vector<EntityId>& out)
    {
        out.push_back(entity);
        if (const auto found = children.find(entity); found != children.end())
            for (const EntityId child : found->second)
                AppendSubtree(children, child, out);
    }
}

std::vector<EntityId> RootAndDescendants(const World& world, EntityId root)
{
    ChildrenOf children;
    if (world.IsRegistered<Parent>())
        world.ForEachComponent<Parent>(
            [&](EntityId child, const Parent& parent) { children[parent.Entity].push_back(child); });
    std::vector<EntityId> group;
    AppendSubtree(children, root, group);
    return group;
}
