#include <world/transform/TransformPropagation.h>

#include <world/registry/Registry.h>

#include <unordered_set>

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
