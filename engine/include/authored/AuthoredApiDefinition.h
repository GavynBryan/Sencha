#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredQueryStatus.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <authored/VerbResult.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

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
    VerbAnswer (*Invoke)(T&, const VerbInvocation&);
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

// What a verb method's return type declares beyond its arguments: its reasons, and its result's kind.
template<typename R>
struct AuthoredVerbReturn
{
    static std::vector<VerbRefusalReason> Refusals() { return {}; }
};

template<AuthoredRefusalReason E>
struct AuthoredVerbReturn<VerbRefusal<E>>
{
    static std::vector<VerbRefusalReason> Refusals() { return VerbRefusalsOf<E>(); }
};

template<typename T, typename E>
struct AuthoredVerbReturn<VerbResult<T, E>>
{
    static std::vector<VerbRefusalReason> Refusals() { return VerbRefusalsOf<E>(); }
    static void DescribeResult(DataFieldSchema& result) { AuthoredValueTraits<T>::Describe(result); }
};

[[nodiscard]] constexpr VerbAnswer AnswerAuthoredVerb(VerbAnswer answer, AuthoredValue*)
{
    return answer;
}

template<AuthoredRefusalReason E>
[[nodiscard]] constexpr VerbAnswer AnswerAuthoredVerb(const VerbRefusal<E>& answer, AuthoredValue*)
{
    return answer.Answer();
}

// Encodes the value only when accepted and asked for.
template<typename T, typename E>
[[nodiscard]] VerbAnswer AnswerAuthoredVerb(const VerbResult<T, E>& answer, AuthoredValue* result)
{
    if (result != nullptr && answer.Answer().Accepted() && answer.Value().has_value())
        *result = AuthoredValueTraits<T>::Encode(*answer.Value());
    return answer.Answer();
}

// One argument as the generated adapter reads it: plainly, or against the provider's World.
template<typename T, typename Provider>
[[nodiscard]] bool DecodeAuthoredArgument(const Provider& provider, const AuthoredValue& value, T& out)
{
    if constexpr (AuthoredWorldDecoded<T>)
        return AuthoredValueTraits<T>::DecodeIn(provider.AuthoredWorld(), value, out);
    else
        return AuthoredValueTraits<T>::Decode(value, out);
}

[[nodiscard]] bool AuthoredTargetAlive(const World& world, EntityId target);

// An absent optional target is not checked.
[[nodiscard]] inline bool AuthoredTargetAlive(const World& world, const std::optional<EntityId>& target)
{
    return !target.has_value() || AuthoredTargetAlive(world, *target);
}

// A const reference argument: borrowed from the argument pack where its type allows, decoded otherwise.
template<typename T>
struct AuthoredHeldArgument
{
    T Value{};

    [[nodiscard]] bool Read(const AuthoredValue& value) { return AuthoredValueTraits<T>::Decode(value, Value); }
    [[nodiscard]] const T& Get() const { return Value; }
};

template<AuthoredBorrowable T>
struct AuthoredHeldArgument<T>
{
    const T* Pointer = nullptr;

    [[nodiscard]] bool Read(const AuthoredValue& value) { return AuthoredValueTraits<T>::Borrow(value, Pointer); }
    [[nodiscard]] const T& Get() const { return *Pointer; }
};
