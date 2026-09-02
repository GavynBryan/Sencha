#pragma once

#include <world/registry/Registry.h>

//=============================================================================
// ComponentStorageTraits
//
// How a serializable component enters storage. Runtime import targets the
// unified World directly; Registry overloads remain editor/legacy adapters
// until the cutover removes runtime registries.
//
// The primary template handles any component: idempotent registration,
// reject-duplicate insertion. Only specialize when a component needs more than
// that (see LocalTransform), and declare the specialization beside the
// component itself: the primary template compiles for any component, so a
// specialization some translation unit fails to see would silently fall back
// to the default behavior in that unit (an ODR violation), and nothing can
// name a component without including its header.
//=============================================================================
template <typename T>
struct ComponentStorageTraits
{
    static void Register(World& world)
    {
        if (!world.IsRegistered<T>())
            world.RegisterComponent<T>();
    }

    static void Register(Registry& registry)
    {
        Register(registry.Components);
    }

    static bool Add(World& world, EntityId entity, T component)
    {
        if (world.HasComponent<T>(entity))
            return false;
        world.AddComponent(entity, component);
        return true;
    }

    static bool Add(Registry& registry, EntityId entity, T component)
    {
        return Add(registry.Components, entity, component);
    }
};
