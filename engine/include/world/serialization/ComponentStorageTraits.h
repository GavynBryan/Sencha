#pragma once

#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <render/StaticMeshComponentStore.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformSchemas.h>
#include <world/transform/TransformStore.h>

#include <utility>

//=============================================================================
// ComponentStorageTraits
//
// Maps a component type to its store type, binary chunk ID, and the Add
// function that inserts a deserialized instance into the registry. Specialize
// for each component type that participates in scene serialization.
//=============================================================================
template <typename T>
struct ComponentStorageTraits;

template <>
struct ComponentStorageTraits<StaticMeshComponent>
{
    using Store = StaticMeshComponentStore;
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::MeshRenders;

    static bool Add(Registry& registry, EntityId entity, StaticMeshComponent component)
    {
        return registry.Components.Ensure<Store>().Add(entity, component);
    }
};

template <>
struct ComponentStorageTraits<CameraComponent>
{
    using Store = CameraStore;
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::Cameras;

    static bool Add(Registry& registry, EntityId entity, CameraComponent component)
    {
        return registry.Components.Ensure<Store>().Add(entity, component);
    }
};

template <>
struct ComponentStorageTraits<TransformComponent<Transform3f>>
{
    using Store = TransformStore<Transform3f>;
    static constexpr std::uint32_t BinaryChunkId = SceneChunk::Transforms;

    static bool Add(Registry& registry, EntityId entity, TransformComponent<Transform3f> component)
    {
        auto& order = registry.Resources.Ensure<TransformPropagationOrderService>();
        auto& hierarchy = registry.Resources.Ensure<TransformHierarchyService>();
        // Register in hierarchy before adding to the store so that propagation
        // order is valid when the store observer runs.
        hierarchy.Register(entity);
        return registry.Components.Ensure<Store>(order).Add(entity, component);
    }
};
