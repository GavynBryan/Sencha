#pragma once

#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/transform/TransformComponents.h>

//=============================================================================
// ComponentStorageTraits
//
// Maps serializable component types to their binary chunk IDs and insertion
// behavior. Storage is the registry's archetype World; there is no per-type
// sparse-set store in the migration path.
//=============================================================================
template <typename T>
struct ComponentStorageTraits;

template <>
struct ComponentStorageTraits<StaticMeshComponent>
{
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::MeshRenders;

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<StaticMeshComponent>())
            registry.Components.RegisterComponent<StaticMeshComponent>();
    }

    static bool Add(Registry& registry, EntityId entity, StaticMeshComponent component)
    {
        if (registry.Components.HasComponent<StaticMeshComponent>(entity))
            return false;
        registry.Components.AddComponent(entity, component);
        return true;
    }
};

template <>
struct ComponentStorageTraits<CameraComponent>
{
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::Cameras;

    static void Register(Registry& registry)
    {
        if (!registry.Components.IsRegistered<CameraComponent>())
            registry.Components.RegisterComponent<CameraComponent>();
    }

    static bool Add(Registry& registry, EntityId entity, CameraComponent component)
    {
        if (registry.Components.HasComponent<CameraComponent>(entity))
            return false;
        registry.Components.AddComponent(entity, component);
        return true;
    }
};

template <>
struct ComponentStorageTraits<LocalTransform>
{
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::Transforms;

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
