#pragma once

#include <ecs/ComponentTypeId.h>

#include <array>
#include <cstddef>
#include <type_traits>

//=============================================================================
// ComponentSet
//
// A named, ordered list of component types. Two distinct uses, deliberately the
// same mechanism:
//
//   - a module's vocabulary, handed to ComponentRegistrar::AddAll;
//   - a feature's own classification of components, owned by the code that
//     defines the classification rather than annotated onto the components.
//
// Order is load-bearing for a registration set: it fixes the dense in-World
// component index and the one-byte wire key (see RegisterEngineComponents), so
// a pack -- which preserves order exactly -- is the representation, and the
// identity freeze harness is what proves the order has not moved.
//
// Registration stays explicit. A set is written by hand and passed by hand;
// nothing self-registers and nothing is enumerated by the linker.
//=============================================================================
template <typename... Components>
struct ComponentSet
{
    static constexpr std::size_t Size = sizeof...(Components);

    // The set's members as stable ids, for a consumer that classifies by id
    // rather than by type -- a replay pass asking whether a component is one
    // its tick restores, for instance.
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

//=============================================================================
// ComponentSetCollection
//
// Every module vocabulary in one place, so ownership can be asserted: a
// component belongs to exactly one set.
//
// This has to be checked over the type lists, not over the registered World.
// Registration is idempotent -- World::RegisterComponent returns the existing
// id and the serializer registry treats an identical tuple as already present --
// so a component listed in two sets still produces exactly one World entry, and
// the second listing leaves no trace to find. The duplicate is only visible
// here, where both occurrences are still in the pack.
//=============================================================================
namespace ComponentSetDetail
{
    template <typename T, typename... Rest>
    inline constexpr std::size_t CountOf = (std::size_t{ std::is_same_v<T, Rest> } + ...);

    template <typename... Ts>
    inline constexpr bool NoDuplicates = ((CountOf<Ts, Ts...> == 1) && ...);

    // Concatenate the sets' packs into one.
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

    // A component in two module vocabularies is an ownership error, and this is
    // where it is still visible.
    static constexpr bool Owned = NoDuplicatesIn(static_cast<Flattened*>(nullptr));
};
