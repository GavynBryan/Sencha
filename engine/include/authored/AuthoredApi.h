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

//=============================================================================
// Declaring and binding a generated authored API
//
// The two explicit calls that make a type's annotated contracts real, and the
// only two: declaring puts its metadata into a World's catalogs, binding puts
// a live object behind it and hands back what owns that. Nothing is found,
// instantiated or registered on anyone's behalf -- the game's vocabulary hook
// and its startup code say exactly which types are exposed and which objects
// answer for them.
//
//   void MyGame::OnRegisterVocabulary(World& world)
//   {
//       AuthoredVocabularyScope vocabulary(world, "game");
//       vocabulary.Declare<Torch>();            // its SENCHA_QUERY members
//       vocabulary.Declare<TorchLitEvent>();    // a SENCHA_EVENT
//       vocabulary.Declare<TorchSystem>();      // its SENCHA_VERB methods
//       (void)vocabulary.Commit();              // the host reads any errors
//   }
//
//   TorchBindings = BindAuthoredApi(engine.TryVerbs(), engine.TryAuthoredQueries(), *Torches);
//   TorchQueries = BindAuthoredApi<Torch>(*engine.TryAuthoredQueries(), world);
//=============================================================================

//-----------------------------------------------------------------------------
// AuthoredApiBindings
//
// The tokens one Bind produced. Destroying or resetting it takes the object
// away from every verb and query it was put behind, so its owner holds it for
// as long as the object is valid and gives it back first -- the same contract
// a single token has, for a whole type at once.
//-----------------------------------------------------------------------------
class AuthoredApiBindings
{
public:
    AuthoredApiBindings() = default;

    AuthoredApiBindings(const AuthoredApiBindings&) = delete;
    AuthoredApiBindings& operator=(const AuthoredApiBindings&) = delete;
    AuthoredApiBindings(AuthoredApiBindings&&) noexcept = default;
    AuthoredApiBindings& operator=(AuthoredApiBindings&&) noexcept = default;

    // Keeps one binding's token. An invalid token is an entry that could not
    // be bound -- not declared in this catalog, bound while one was being
    // dispatched, or offered no dispatcher -- and its name is kept instead so
    // the owner can say which.
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

    // The entries the type declares that nothing was put behind. Empty when
    // the whole type is live.
    [[nodiscard]] std::span<const std::string> Unbound() const { return Unbound_; }

private:
    std::vector<VerbBindingToken> Verbs;
    std::vector<AuthoredQueryBindingToken> Queries;
    std::vector<std::string> Unbound_;
};

namespace AuthoredApiDetail
{
    // A component query's answer: the member, read from the entity's row
    // without registering a write. Unavailable, not a default, when the entity
    // has no such row.
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

//-----------------------------------------------------------------------------
// AuthoredVocabularyScope
//
// One provider's declarations into a World's verb, query and event catalogs,
// committed as one. Every batch is checked against its catalog before any
// catalog changes, so a provider whose query conflicts publishes no verbs and
// no events either: a module that fails to register leaves none of its
// vocabulary behind, in a runtime World or an editor's.
//
// A World without catalogs -- one nothing installed vocabulary into -- takes
// no declarations, and its commit reports false.
//-----------------------------------------------------------------------------
class AuthoredVocabularyScope
{
public:
    AuthoredVocabularyScope(World& world, std::string provider);

    AuthoredVocabularyScope(const AuthoredVocabularyScope&) = delete;
    AuthoredVocabularyScope& operator=(const AuthoredVocabularyScope&) = delete;
    AuthoredVocabularyScope(AuthoredVocabularyScope&&) = delete;
    AuthoredVocabularyScope& operator=(AuthoredVocabularyScope&&) = delete;

    // Declares everything T's companion describes: its verb and query
    // methods, its queryable members, or the event it is.
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

    // Publishes every batch, or none. False leaves every catalog as it was
    // and leaves the reasons where the host reads installation errors.
    [[nodiscard]] bool Commit();

private:
    void DeclareVerb(VerbDefinition definition);
    void DeclareQuery(AuthoredQueryDefinition definition);
    void DeclareEvent(AuthoredEventDefinition definition);

    std::optional<VerbRegistrationScope> Verbs;
    std::optional<AuthoredQueryRegistrationScope> Queries;
    std::optional<AuthoredEventRegistrationScope> Events;
};

// Puts `target` behind every annotated verb and query method of its type.
// Each entry is found by the name it was declared under, once, here; nothing
// after this looks a name up. A null dispatcher binds nothing of its kind, and
// the type's entries of that kind are reported unbound.
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

// Puts a World behind every queryable member of component C: each query reads
// that member from whichever entity it is asked about, in this World.
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
