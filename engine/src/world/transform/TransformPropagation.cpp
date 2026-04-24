#include <world/transform/TransformPropagation.h>

#include <world/registry/Registry.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct TransformNode
    {
        Transform3f Local = Transform3f::Identity();
        EntityId Parent;
        bool HasParent = false;
        Transform3f World = Transform3f::Identity();
        uint8_t State = 0; // 0 = unvisited, 1 = visiting, 2 = done
    };

    bool ComputeWorldTransform(
        EntityIndex entity,
        std::unordered_map<EntityIndex, TransformNode>& nodes)
    {
        auto it = nodes.find(entity);
        if (it == nodes.end())
            return false;

        TransformNode& node = it->second;
        if (node.State == 2)
            return true;

        if (node.State == 1)
        {
            // Cycle safety: keep the entity locally coherent and stop walking
            // this branch. Hierarchy validation should prevent this upstream.
            node.World = node.Local;
            node.State = 2;
            return true;
        }

        node.State = 1;

        if (node.HasParent && node.Parent.IsValid())
        {
            auto parentIt = nodes.find(node.Parent.Index);
            if (parentIt != nodes.end() && ComputeWorldTransform(node.Parent.Index, nodes))
                node.World = parentIt->second.World * node.Local;
            else
                node.World = node.Local;
        }
        else
        {
            node.World = node.Local;
        }

        node.State = 2;
        return true;
    }
}

void TransformPropagationSystem::Propagate()
{
    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        return;
    }

    std::unordered_map<EntityIndex, TransformNode> nodes;
    nodes.reserve(Target.CountComponents<LocalTransform>());

    Query<Read<LocalTransform>, With<WorldTransform>> locals(Target);
    locals.ForEachChunk([&](auto& view)
    {
        const auto local = view.template Read<LocalTransform>();
        const EntityIndex* entities = view.Entities();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            TransformNode& node = nodes[entities[i]];
            node.Local = local[i].Value;
            node.World = local[i].Value;
        }
    });

    if (nodes.empty())
        return;

    if (Target.IsRegistered<Parent>())
    {
        Query<Read<Parent>, With<LocalTransform>, With<WorldTransform>> parents(Target);
        parents.ForEachChunk([&](auto& view)
        {
            const auto parent = view.template Read<Parent>();
            const EntityIndex* entities = view.Entities();

            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                auto nodeIt = nodes.find(entities[i]);
                if (nodeIt == nodes.end())
                    continue;

                nodeIt->second.Parent = parent[i].Entity;
                nodeIt->second.HasParent = parent[i].Entity.IsValid();
            }
        });
    }

    for (auto& [entity, node] : nodes)
        ComputeWorldTransform(entity, nodes);

    Query<Read<LocalTransform>, Write<WorldTransform>> worlds(Target);
    worlds.ForEachChunk([&](auto& view)
    {
        auto world = view.template Write<WorldTransform>();
        const EntityIndex* entities = view.Entities();

        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            auto nodeIt = nodes.find(entities[i]);
            if (nodeIt != nodes.end())
                world[i].Value = nodeIt->second.World;
        }
    });
}

void PropagateTransforms(std::span<Registry*> registries)
{
    std::unordered_set<Registry*> seen;
    for (Registry* registry : registries)
    {
        if (registry == nullptr || !seen.insert(registry).second)
            continue;

        PropagateTransforms(registry->Components);
    }
}
