#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredQueryStatus.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/ComponentTypeId.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

class World;

//=============================================================================
// AuthoredApiDefinition
//
// What sencha-component-codegen writes for a type that exposes authored verbs,
// queries or events, and what the explicit Declare and Bind helpers read. A
// generated specialization is plain C++ a person could have typed: one
// Describe function per entry that builds the contract the way a hand-written
// declaration would, one adapter per entry that decodes each argument slot and
// calls the annotated method, and a table listing them.
//
// This header is what a companion includes, so it stays free of the World and
// the dispatchers: a component header that exposes a query pulls in value
// traits and schema types, never the machinery that runs anything.
//=============================================================================

// Primary left undefined: only generated specializations exist.
template<typename T>
struct AuthoredApiDefinition;

// One annotated verb method on T: its persisted name, the declaration it
// makes, and the adapter a dispatcher calls.
template<typename T>
struct AuthoredVerbEntry
{
    std::string_view Name;
    VerbDefinition (*Describe)();
    VerbAdmission (*Invoke)(T&, const VerbInvocation&);
};

// One annotated query method on T.
template<typename T>
struct AuthoredQueryEntry
{
    std::string_view Name;
    AuthoredQueryDefinition (*Describe)();
    AuthoredQueryStatus (*Evaluate)(const T&, std::span<const AuthoredValue>, AuthoredValue&);
};

// One queryable component member. The member pointer is the whole of what
// reading it needs; the reader is instantiated where the component's queries
// are bound against a World.
template<auto Member>
struct AuthoredFieldQuery
{
    static constexpr auto Pointer = Member;
    std::string_view Name;
    AuthoredQueryDefinition (*Describe)();
};

template<typename T>
concept HasAuthoredVerbs = requires {
    AuthoredApiDefinition<T>::Verbs;
    typename AuthoredApiDefinition<T>::Target;
};

template<typename T>
concept HasAuthoredQueries = requires {
    AuthoredApiDefinition<T>::Queries;
    typename AuthoredApiDefinition<T>::Target;
};

template<typename T>
concept HasAuthoredFieldQueries = requires { AuthoredApiDefinition<T>::FieldQueries; };

template<typename E>
concept HasAuthoredEventDefinition = requires {
    AuthoredApiDefinition<E>::EventName;
    AuthoredApiDefinition<E>::DescribeEvent();
};

// The persisted identity a target or event source names, for a component
// declared either by annotation or by hand. Resolved from the type the author
// wrote, so a misspelled component is a compile error in the companion rather
// than a string nobody checks.
template<typename C>
[[nodiscard]] std::string AuthoredComponentIdentity()
{
    return std::string(ResolveComponentName<C>());
}

//-----------------------------------------------------------------------------
// Query results
//
// A query may return T, or std::optional<T> when it can have nothing to say.
// Either way the declared result is T: an empty optional is answered as
// Unavailable, never as a value called "absent".
//-----------------------------------------------------------------------------

template<typename R>
struct AuthoredQueryValue
{
    using Type = R;
};

template<typename T>
struct AuthoredQueryValue<std::optional<T>>
{
    using Type = T;
};

template<typename R>
using AuthoredQueryValueType = typename AuthoredQueryValue<R>::Type;

template<typename R>
struct AuthoredQueryResultTraits
{
    static void Describe(DataFieldSchema& result)
    {
        AuthoredValueTraits<AuthoredQueryValueType<R>>::Describe(result);
    }
};

template<typename R>
[[nodiscard]] AuthoredQueryStatus AnswerAuthoredQuery(const R& answer, AuthoredValue& result)
{
    if constexpr (IsStdOptional<R>)
    {
        if (!answer.has_value())
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValueTraits<typename R::value_type>::Encode(*answer);
    }
    else
    {
        result = AuthoredValueTraits<R>::Encode(answer);
    }
    return AuthoredQueryStatus::Value;
}

// The argument in `slot`, or the absent value past the end: a caller that
// supplied too few is refused by decoding, not by reading past its span.
[[nodiscard]] inline const AuthoredValue& AuthoredArgumentAt(std::span<const AuthoredValue> arguments,
                                                             std::size_t slot)
{
    static const AuthoredValue absent;
    return slot < arguments.size() ? arguments[slot] : absent;
}

// The single argument a component query takes: the entity whose row it reads,
// expected to carry the component.
template<typename C>
void DescribeAuthoredComponentTarget(DataFieldSchema& arguments)
{
    DataFieldSchema field;
    field.Key = "entity";
    AuthoredValueTraits<EntityId>::Describe(field);
    field.Reference.ComponentIdentity = AuthoredComponentIdentity<C>();
    arguments.Children.push_back(std::move(field));
}
