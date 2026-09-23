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

// The shapes sencha-component-codegen emits into companions, and the helpers
// generated code calls. Kept free of the World and the dispatchers, since
// component headers include their companions.

// Primary left undefined: only generated specializations exist.
template<typename T>
struct AuthoredApiDefinition;

template<typename T>
struct AuthoredVerbEntry
{
    std::string_view Name;
    VerbDefinition (*Describe)();
    VerbAdmission (*Invoke)(T&, const VerbInvocation&);
};

template<typename T>
struct AuthoredQueryEntry
{
    std::string_view Name;
    AuthoredQueryDefinition (*Describe)();
    AuthoredQueryStatus (*Evaluate)(const T&, std::span<const AuthoredValue>, AuthoredValue&);
};

// Data only: the reader is instantiated where the queries are bound.
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

// Declared by SENCHA_COMPONENT or SENCHA_DECLARE_COMPONENT_TYPE. A TypeSchema
// alone does not make a type a component.
template<typename C>
concept AuthoredComponentType = HasComponentTypeKey<C>;

template<typename C>
[[nodiscard]] std::string AuthoredComponentIdentity()
{
    static_assert(AuthoredComponentType<C>,
                  "SENCHA_TARGET and SENCHA_EVENT_SOURCE name a component: this type has no "
                  "SENCHA_COMPONENT or SENCHA_DECLARE_COMPONENT_TYPE identity");
    return std::string(ComponentTypeKey<C>::Name);
}

// A query returning std::optional<T> declares a T result; an empty optional
// answers Unavailable.
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

// The absent value past the end, which then fails to decode.
[[nodiscard]] inline const AuthoredValue& AuthoredArgumentAt(std::span<const AuthoredValue> arguments,
                                                             std::size_t slot)
{
    static const AuthoredValue absent;
    return slot < arguments.size() ? arguments[slot] : absent;
}

template<typename C>
void DescribeAuthoredComponentTarget(DataFieldSchema& arguments)
{
    DataFieldSchema field;
    field.Key = "entity";
    AuthoredValueTraits<EntityId>::Describe(field);
    field.Reference.ComponentIdentity = AuthoredComponentIdentity<C>();
    arguments.Children.push_back(std::move(field));
}
