#pragma once

// ComponentTraits<T>: opt-in specialization point for lifecycle hooks.
// Default specialization is trivial — zero overhead for components without hooks.
//
// To add hooks for a component type T, specialize this template near T's definition:
//
//   template <>
//   struct ComponentTraits<MyComponent>
//   {
//       static constexpr bool HasOnAdd    = true;
//       static constexpr bool HasOnRemove = true;
//
//       static void OnAdd(MyComponent& component, World& world, EntityId entity) { ... }
//       static void OnRemove(const MyComponent& component, World& world, EntityId entity) { ... }
//   };
//
// Hooks run synchronously at command-buffer flush.
// Hooks must not perform structural ECS mutations (AddComponent, RemoveComponent,
// CreateEntity, DestroyEntity) — see docs/ecs/MigrationPlan.md §Lifecycle hooks.

template <typename T>
struct ComponentTraits
{
    static constexpr bool HasOnAdd    = false;
    static constexpr bool HasOnRemove = false;
};
