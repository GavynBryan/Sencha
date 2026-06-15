#pragma once

#include <core/identity/StrongId.h>
#include <core/metadata/TypeSchema.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// ComponentTypeId
//
// A stable, content-addressed component contract identity. Derived from a
// stable name the component declares once, it is identical in every module
// that compiles the same name — no RTTI, no link-time symbol merging, no
// per-build drift. This replaces std::type_index(typeid(T)) as the World's
// component-lookup key (see docs/plans/sencha-level-editor/01-...md, Pillar 1).
//
// Identity is a *contract* key, not a proof of C++ type equivalence: two
// modules declaring the same stable name are claiming the same component
// contract. The engine validates obvious mismatches loudly at registration
// (size/alignment/tag-ness), but does not attempt deeper structural identity.
//=============================================================================
using ComponentTypeId = StrongId<struct ComponentTypeIdTag, std::uint64_t>;

// FNV-1a over the name, constexpr so it folds at compile time and is identical
// in every translation unit and every module by the language rules — there is
// no external linkage to merge. Collisions between *distinct* names are
// astronomically unlikely at 64 bits and are caught at registration when the
// colliding components disagree on storage metadata (World::RegisterComponent).
constexpr ComponentTypeId MakeComponentTypeId(std::string_view name)
{
    std::uint64_t h = 14695981039346656037ull;
    for (char c : name)
    {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    if (h == 0)
        h = 1; // zero is StrongId's invalid sentinel; never hand it out.
    return ComponentTypeId{ h };
}

//-----------------------------------------------------------------------------
// Identity binding
//
// Two ways a component gets an identity, resolved in this priority order:
//
//  1. An explicit ComponentTypeKey<T> specialization — for pure-runtime,
//     never-serialized components (tags like Parent, WorldTransform) and for
//     tests. Declare it with the SENCHA_DECLARE_COMPONENT_TYPE macro below.
//  2. TypeSchema<T>::Name — every serializable component already declares this
//     as its JSON key; for those it doubles as the stable identity name. One
//     source of truth, but note the contract: that name is a stable wire key,
//     not casual UI copy.
//
// An explicit key overrides schema identity (migration / runtime-only types).
// At least one source must exist or resolution is a hard compile error.
//-----------------------------------------------------------------------------

// Primary left undefined: only specializations carry an identity.
template <typename T> struct ComponentTypeKey;

template <typename T>
concept HasComponentTypeKey = requires { ComponentTypeKey<T>::Id; };

template <typename T>
concept HasComponentName = requires { TypeSchema<T>::Name; };

template <typename T> requires HasComponentName<T>
constexpr ComponentTypeId ComponentTypeIdOf()
{
    return MakeComponentTypeId(TypeSchema<T>::Name);
}

template <typename>
inline constexpr bool ComponentTypeIdAlwaysFalse = false;

// The single resolution point the World uses. Key-over-schema; hard error if
// neither source exists.
template <typename T>
constexpr ComponentTypeId ResolveComponentTypeId()
{
    if constexpr (HasComponentTypeKey<T>)
        return ComponentTypeKey<T>::Id;
    else if constexpr (HasComponentName<T>)
        return ComponentTypeIdOf<T>();
    else
        static_assert(ComponentTypeIdAlwaysFalse<T>,
                      "Component type has no stable identity: declare a "
                      "TypeSchema<T>::Name or SENCHA_DECLARE_COMPONENT_TYPE(T, \"vendor.name\").");
}

// The human/debug-readable stable name behind a component's identity, for
// diagnostics and registration-conflict messages. Mirrors ResolveComponentTypeId's
// priority. Returns empty only for the undeclared case the static_assert above
// already rejects at the id-resolution call site.
template <typename T>
constexpr std::string_view ResolveComponentName()
{
    if constexpr (requires { ComponentTypeKey<T>::Name; })
        return ComponentTypeKey<T>::Name;
    else if constexpr (HasComponentName<T>)
        return TypeSchema<T>::Name;
    else
        return std::string_view{};
}

//-----------------------------------------------------------------------------
// SENCHA_DECLARE_COMPONENT_TYPE(Type, "vendor.name")
//
// Binds a stable identity to a component that has no TypeSchema (or that must
// override its schema identity). Use at namespace (global) scope, once, next to
// the type. The name must be a lower-case, dotted, namespaced identifier
// (e.g. "sencha.world_transform", "editor.brush") — never an unqualified label.
//-----------------------------------------------------------------------------
#define SENCHA_DECLARE_COMPONENT_TYPE(Type, StableName)                       \
    template <>                                                               \
    struct ComponentTypeKey<Type>                                            \
    {                                                                         \
        static constexpr std::string_view  Name = StableName;                \
        static constexpr ComponentTypeId   Id   = MakeComponentTypeId(StableName); \
    }
