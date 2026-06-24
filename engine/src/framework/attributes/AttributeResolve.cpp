#include <framework/attributes/AttributeResolve.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSet.h>

#include <ecs/World.h>

#include <utility>

void ResolveAttributes(World& world)
{
    const AttributeRegistry* registry = std::as_const(world).TryGetResource<AttributeRegistry>();
    if (registry == nullptr)
        return;

    // Mutable ForEachComponent bumps the AttributeSet change version, so
    // Changed<AttributeSet> downstream sees the recomputed Current values.
    world.ForEachComponent<AttributeSet>([registry](EntityId, AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = registry->Clamp(set.Ids[i], set.Base[i]);
    });
}
