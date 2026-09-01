#pragma once

#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <world/registry/Registry.h>

#include <cstdint>

//=============================================================================
// ComponentStorageTraits
//
// Maps serializable component types to their binary chunk IDs and insertion
// behavior. Runtime import targets the unified World directly; Registry overloads
// remain editor/legacy adapters until the cutover removes runtime registries.
//
// The primary template handles any component whose TypeSchema declares a
// SceneChunkId: idempotent registration, reject-duplicate insertion. Only
// specialize when a component needs more than that (see LocalTransform).
//
// All specializations must live in this header. The primary template compiles
// for any component with a TypeSchema, so a specialization in a header that
// some translation unit fails to include would silently fall back to the
// default behavior in that unit (an ODR violation).
//=============================================================================
template <typename T>
struct ComponentStorageTraits
{
    static constexpr std::uint32_t BinaryChunkId = TypeSchema<T>::SceneChunkId;

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

