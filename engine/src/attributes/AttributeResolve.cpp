#include <attributes/AttributeResolve.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>

#include <ecs/World.h>

#include <utility>

void ResetAttributesToBase(World& world)
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

void ClampAttributes(World& world)
{
    if (!world.IsRegistered<AttributeSet>())
        return;
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
