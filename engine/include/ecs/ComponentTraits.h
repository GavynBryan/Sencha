#pragma once

#include <ecs/EntityId.h>

// ComponentTraits<T>: opt-in specialization point for lifecycle hooks.
// Default specialization is trivial — zero overhead for components without hooks.
//
// To add hooks for a component type T, specialize this template near T's definition:
//
//   template <>
//   struct ComponentTraits<MyComponent>
//   {
//       static void OnAdd(MyComponent& component, World& world, EntityId entity) { ... }
//       static void OnRemove(const MyComponent& component, World& world, EntityId entity) { ... }
//   };
//
// Hooks run synchronously at command-buffer flush.
// Hooks must not perform structural ECS mutations (AddComponent, RemoveComponent,
// CreateEntity, DestroyEntity) — see docs/ecs/MigrationPlan.md §Lifecycle hooks.

class World;

template <typename T>
struct ComponentTraits
{
};

template <typename T>
concept ComponentHasOnAdd =
    requires(T& component, World& world, EntityId entity)
    {
        ComponentTraits<T>::OnAdd(component, world, entity);
    };

template <typename T>
concept ComponentHasOnRemove =
    requires(const T& component, World& world, EntityId entity)
    {
        ComponentTraits<T>::OnRemove(component, world, entity);
    };
