#pragma once

#include <ecs/ComponentTypeId.h>

#include <array>
#include <cstddef>
#include <type_traits>

// An ordered list of component types: a module's vocabulary for
// ComponentRegistrar::AddAll, or a feature's own classification of components,
// owned by the code that defines the classification rather than annotated onto
// the components themselves.
//
// Order is load-bearing -- it fixes the dense in-World component index and the
// one-byte wire key -- and a pack preserves it exactly.
template <typename... Components>
struct ComponentSet
{
    static constexpr std::size_t Size = sizeof...(Components);

    [[nodiscard]] static const std::array<ComponentTypeId, Size>& Ids()
    {
        static const std::array<ComponentTypeId, Size> ids{
            ResolveComponentTypeId<Components>()... };
        return ids;
    }

    [[nodiscard]] static bool Contains(ComponentTypeId type)
    {
        for (const ComponentTypeId id : Ids())
            if (id == type)
                return true;
        return false;
    }

    template <typename T>
    static constexpr bool Contains_v = (std::is_same_v<T, Components> || ...);
};

// Every module vocabulary in one place, so ownership can be asserted.
//
// Checked over the type lists rather than the registered World: registration is
// idempotent, so a component listed in two sets still yields one World entry
// and the second listing leaves no trace. Here both occurrences survive.
namespace ComponentSetDetail
{
    template <typename T, typename... Rest>
    inline constexpr std::size_t CountOf = (std::size_t{ std::is_same_v<T, Rest> } + ...);

    template <typename... Ts>
    inline constexpr bool NoDuplicates = ((CountOf<Ts, Ts...> == 1) && ...);

    template <typename...> struct Flatten;

    template <>
    struct Flatten<> { using Type = ComponentSet<>; };

    template <typename... A>
    struct Flatten<ComponentSet<A...>> { using Type = ComponentSet<A...>; };

    template <typename... A, typename... B, typename... Rest>
    struct Flatten<ComponentSet<A...>, ComponentSet<B...>, Rest...>
        : Flatten<ComponentSet<A..., B...>, Rest...> {};
}

template <typename... Sets>
struct ComponentSetCollection
{
    using Flattened = typename ComponentSetDetail::Flatten<Sets...>::Type;

    template <typename... Components>
    static constexpr bool NoDuplicatesIn(ComponentSet<Components...>*)
    {
        return ComponentSetDetail::NoDuplicates<Components...>;
    }

    static constexpr bool Owned = NoDuplicatesIn(static_cast<Flattened*>(nullptr));
};
