#include <world/transform/TransformPropagation.h>

#include <math/geometry/3d/Transform3d.h>
#include <world/registry/Registry.h>

#include <unordered_set>

void PropagateTransforms(std::span<Registry*> registries)
{
    std::unordered_set<Registry*> seen;
    for (Registry* registry : registries)
    {
        if (registry == nullptr || !seen.insert(registry).second)
            continue;

        auto* transforms = registry->Components.TryGet<TransformStore<Transform3f>>();
        auto* hierarchy = registry->Resources.TryGet<TransformHierarchyService>();
        auto* order = registry->Resources.TryGet<TransformPropagationOrderService>();

        if (transforms == nullptr || hierarchy == nullptr || order == nullptr)
            continue;

        PropagateTransforms(*transforms, *hierarchy, *order);
    }
}
