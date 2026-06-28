#include <framework/attributes/AttributeResolve.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSet.h>

#include <ecs/World.h>

#include <utility>

void ResetAttributesToBase(World& world)
{
    // Mutable ForEachComponent bumps the AttributeSet change version, so
    // Changed<AttributeSet> downstream sees the recomputed Current values.
    world.ForEachComponent<AttributeSet>([](EntityId, AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = set.Base[i];
    });
}

void ClampAttributes(World& world)
{
    const AttributeRegistry* registry = std::as_const(world).TryGetResource<AttributeRegistry>();
    if (registry == nullptr)
        return;

    world.ForEachComponent<AttributeSet>([registry](EntityId, AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = registry->Clamp(set.Ids[i], set.Current[i]);
    });
}

void ResolveAttributes(World& world)
{
    ResetAttributesToBase(world);
    ClampAttributes(world);
}
