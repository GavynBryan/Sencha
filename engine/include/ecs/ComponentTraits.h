#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

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
// CreateEntity, DestroyEntity) — see docs/ecs/component-traits.md.

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

//=============================================================================
// DerivedComponents
//
// The components T cannot work without, declared beside T:
//
//   template <>
//   struct ComponentTraits<CharacterMovement>
//   {
//       using DerivedComponents = std::tuple<MovementIntent, KinematicState>;
//   };
//
// A set, not a sequence. Adding T provisions everything in it that the entity
// does not already carry, default-constructed, through the same typed add --
// which applies each one's own owed set in turn, so the closure is transitive
// by construction, duplicates collapse into the first one added, and a cycle
// terminates on the component that is already there. Nothing here is ordered:
// an owed component's OnAdd may not assume a sibling has arrived yet.
//
// This is for scratch a system reads and writes every tick -- the per-tick
// request, the resolved coefficients, the composed output -- where a missing
// column means the entity silently stops matching the query that would have
// moved it. It is not for state that has to be seeded from something: the
// provision is T{} and nothing else, so a component whose correct initial value
// depends on the entity belongs with whatever computes it.
//=============================================================================
template <typename T>
concept ComponentOwesComponents =
    requires { typename ComponentTraits<T>::DerivedComponents; };

namespace ComponentTraitsDetail
{
    template <typename Owed, std::size_t... Index>
    void CollectOwedIds(std::vector<ComponentTypeId>& out, std::index_sequence<Index...>)
    {
        (out.push_back(ResolveComponentTypeId<std::tuple_element_t<Index, Owed>>()), ...);
    }
}

// T's declared set as stable ids: the tuple expanded once, in declaration
// order, duplicates included and nothing folded. Empty for a component that
// owes nothing.
//
// Here rather than beside either consumer because there are two of them and
// they sit on opposite sides of a header dependency: a World records this per
// component so a type-erased add can provision by id, and a sealed
// WorldComponentSchema folds it into the transitive closure the batch importer
// ORs into an archetype signature. One expansion of the tuple, in one place.
template <typename T>
[[nodiscard]] inline std::vector<ComponentTypeId> DeclaredOwedIds()
{
    std::vector<ComponentTypeId> ids;
    if constexpr (ComponentOwesComponents<T>)
    {
        using Owed = typename ComponentTraits<T>::DerivedComponents;
        ids.reserve(std::tuple_size_v<Owed>);
        ComponentTraitsDetail::CollectOwedIds<Owed>(
            ids, std::make_index_sequence<std::tuple_size_v<Owed>>{});
    }
    return ids;
}
