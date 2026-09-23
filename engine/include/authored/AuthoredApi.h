#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <ecs/World.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Declare and Bind for generated authored APIs. See docs/gameplay/authored-api.md.

// Owns the tokens one BindAuthoredApi produced; resetting it unbinds the target.
class AuthoredApiBindings
{
public:
    AuthoredApiBindings() = default;

    AuthoredApiBindings(const AuthoredApiBindings&) = delete;
    AuthoredApiBindings& operator=(const AuthoredApiBindings&) = delete;
    AuthoredApiBindings(AuthoredApiBindings&&) noexcept = default;
    AuthoredApiBindings& operator=(AuthoredApiBindings&&) noexcept = default;

    // An invalid token records the entry's name in Unbound().
    void Hold(VerbBindingToken token, std::string_view name)
    {
        if (token.IsValid())
            Verbs.push_back(std::move(token));
        else
            Unbound_.emplace_back(name);
    }

    void Hold(AuthoredQueryBindingToken token, std::string_view name)
    {
        if (token.IsValid())
            Queries.push_back(std::move(token));
        else
            Unbound_.emplace_back(name);
    }

    void Reset()
    {
        Verbs.clear();
        Queries.clear();
        Unbound_.clear();
    }

    [[nodiscard]] std::size_t BoundCount() const { return Verbs.size() + Queries.size(); }

    [[nodiscard]] std::span<const std::string> Unbound() const { return Unbound_; }

private:
    std::vector<VerbBindingToken> Verbs;
    std::vector<AuthoredQueryBindingToken> Queries;
    std::vector<std::string> Unbound_;
};

namespace AuthoredApiDetail
{
    // Const access, so reading registers no write.
    template<typename C, auto Member>
    AuthoredQueryStatus ReadComponentField(const World& world,
                                           std::span<const AuthoredValue> arguments,
                                           AuthoredValue& result)
    {
        EntityId entity;
        if (!AuthoredValueTraits<EntityId>::Decode(AuthoredArgumentAt(arguments, 0), entity))
            return AuthoredQueryStatus::InvalidArguments;
        if (!world.IsRegistered<C>())
            return AuthoredQueryStatus::Unavailable;
        const C* row = world.TryGet<C>(entity);
        if (row == nullptr)
            return AuthoredQueryStatus::Unavailable;
        return AnswerAuthoredQuery(row->*Member, result);
    }
} // namespace AuthoredApiDetail

// One provider's declarations into all three catalogs, committed together.
// A World without catalogs takes nothing, and Commit returns false.
class AuthoredVocabularyScope
{
public:
    AuthoredVocabularyScope(World& world, std::string provider);

    AuthoredVocabularyScope(const AuthoredVocabularyScope&) = delete;
    AuthoredVocabularyScope& operator=(const AuthoredVocabularyScope&) = delete;
    AuthoredVocabularyScope(AuthoredVocabularyScope&&) = delete;
    AuthoredVocabularyScope& operator=(AuthoredVocabularyScope&&) = delete;

    template<typename T>
    void Declare()
    {
        static_assert(HasAuthoredVerbs<T> || HasAuthoredQueries<T> || HasAuthoredFieldQueries<T>
                          || HasAuthoredEventDefinition<T>,
                      "this type exposes no authored contract: annotate it with SENCHA_VERB, "
                      "SENCHA_QUERY or SENCHA_EVENT, and include its companion");
        if constexpr (HasAuthoredVerbs<T>)
        {
            for (const AuthoredVerbEntry<T>& entry : AuthoredApiDefinition<T>::Verbs)
                DeclareVerb(entry.Describe());
        }
        if constexpr (HasAuthoredQueries<T>)
        {
            for (const AuthoredQueryEntry<T>& entry : AuthoredApiDefinition<T>::Queries)
                DeclareQuery(entry.Describe());
        }
        if constexpr (HasAuthoredFieldQueries<T>)
        {
            std::apply([this](const auto&... query) { (DeclareQuery(query.Describe()), ...); },
                       AuthoredApiDefinition<T>::FieldQueries);
        }
        if constexpr (HasAuthoredEventDefinition<T>)
            DeclareEvent(AuthoredApiDefinition<T>::DescribeEvent());
    }

    [[nodiscard]] bool Commit();

private:
    void DeclareVerb(VerbDefinition definition);
    void DeclareQuery(AuthoredQueryDefinition definition);
    void DeclareEvent(AuthoredEventDefinition definition);

    std::optional<VerbRegistrationScope> Verbs;
    std::optional<AuthoredQueryRegistrationScope> Queries;
    std::optional<AuthoredEventRegistrationScope> Events;
};

// A null dispatcher leaves that kind's entries in Unbound().
template<typename T>
    requires(HasAuthoredVerbs<T> || HasAuthoredQueries<T>)
[[nodiscard]] AuthoredApiBindings BindAuthoredApi(VerbDispatcher* verbs,
                                                  AuthoredQueryDispatcher* queries,
                                                  T& target)
{
    using Api = AuthoredApiDefinition<T>;
    AuthoredApiBindings bindings;
    if constexpr (HasAuthoredVerbs<T>)
    {
        [&]<std::size_t... Index>(std::index_sequence<Index...>) {
            (bindings.Hold(verbs != nullptr
                               ? verbs->template Bind<Api::Verbs[Index].Invoke>(
                                     verbs->Registry().Find(Api::Verbs[Index].Name), target)
                               : VerbBindingToken{},
                           Api::Verbs[Index].Name),
             ...);
        }(std::make_index_sequence<Api::Verbs.size()>{});
    }
    if constexpr (HasAuthoredQueries<T>)
    {
        [&]<std::size_t... Index>(std::index_sequence<Index...>) {
            (bindings.Hold(queries != nullptr
                               ? queries->template Bind<Api::Queries[Index].Evaluate>(
                                     queries->Registry().Find(Api::Queries[Index].Name),
                                     std::as_const(target))
                               : AuthoredQueryBindingToken{},
                           Api::Queries[Index].Name),
             ...);
        }(std::make_index_sequence<Api::Queries.size()>{});
    }
    return bindings;
}

template<typename C>
    requires HasAuthoredFieldQueries<C>
[[nodiscard]] AuthoredApiBindings BindAuthoredApi(AuthoredQueryDispatcher& queries,
                                                  const World& world)
{
    AuthoredApiBindings bindings;
    std::apply(
        [&](const auto&... query) {
            (bindings.Hold(
                 queries.template Bind<&AuthoredApiDetail::ReadComponentField<
                     C, std::remove_cvref_t<decltype(query)>::Pointer>>(
                     queries.Registry().Find(query.Name), world),
                 query.Name),
             ...);
        },
        AuthoredApiDefinition<C>::FieldQueries);
    return bindings;
}
