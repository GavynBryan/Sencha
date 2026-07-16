#include <attributes/AttributeResolve.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>

#include <ecs/EntityStore.h>

#include <utility>

void ResetAttributesToBase(EntityStore& world)
{
    // A frame span may include worlds that host no attribute-bearing entities
    // (the global registry before a game registers components there).
    if (!world.IsRegistered<AttributeSet>())
        return;
    // Mutable ForEachComponent bumps the AttributeSet change version, so
    // Changed<AttributeSet> downstream sees the recomputed Current values.
    world.ForEachComponent<AttributeSet>([](EntityId, AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = set.Base[i];
    });
}

void ClampAttributes(EntityStore& world, const AttributeRegistry& attributes)
{
    if (!world.IsRegistered<AttributeSet>())
        return;

    world.ForEachComponent<AttributeSet>([&attributes](EntityId, AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = attributes.Clamp(set.Ids[i], set.Current[i]);
    });
}

void ResolveAttributes(EntityStore& world, const AttributeRegistry& attributes)
{
    ResetAttributesToBase(world);
    ClampAttributes(world, attributes);
}
