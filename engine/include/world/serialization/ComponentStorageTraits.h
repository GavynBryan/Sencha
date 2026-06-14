#pragma once

#include <core/metadata/TypeSchema.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>

//=============================================================================
// ComponentStorageTraits
//
// Maps serializable component types to their binary chunk IDs and insertion
// behavior. Storage is the registry's archetype World.
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

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<T>())
            registry.Components.RegisterComponent<T>();
    }

    static bool Add(Registry& registry, EntityId entity, T component)
    {
        if (registry.Components.HasComponent<T>(entity))
            return false;
        registry.Components.AddComponent(entity, component);
        return true;
    }
};

// LocalTransform is the one structural special case: WorldTransform and Parent
// are not serialized themselves (hierarchy travels in the Hierarchy chunk, and
// WorldTransform is derived), so they register alongside LocalTransform, and
// every loaded LocalTransform seeds a matching WorldTransform for propagation.
template <>
struct ComponentStorageTraits<LocalTransform>
{
    static constexpr std::uint32_t BinaryChunkId = TypeSchema<LocalTransform>::SceneChunkId;

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<LocalTransform>())
            registry.Components.RegisterComponent<LocalTransform>();
        if (!registry.Components.IsRegistered<WorldTransform>())
            registry.Components.RegisterComponent<WorldTransform>();
        if (!registry.Components.IsRegistered<Parent>())
            registry.Components.RegisterComponent<Parent>();
    }

    static bool Add(Registry& registry, EntityId entity, LocalTransform component)
    {
        if (registry.Components.HasComponent<LocalTransform>(entity))
            return false;

        registry.Components.AddComponent(entity, component);
        if (!registry.Components.HasComponent<WorldTransform>(entity))
            registry.Components.AddComponent(entity, WorldTransform{ component.Value });
        return true;
    }
};
