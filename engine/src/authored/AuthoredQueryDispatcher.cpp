#include <authored/AuthoredQueryDispatcher.h>

#include <cassert>

const char* AuthoredQueryStatusName(AuthoredQueryStatus status)
{
    switch (status)
    {
    case AuthoredQueryStatus::Value: return "value";
    case AuthoredQueryStatus::Unavailable: return "unavailable";
    case AuthoredQueryStatus::InvalidArguments: return "invalid arguments";
    case AuthoredQueryStatus::Unbound: return "no implementation";
    case AuthoredQueryStatus::Stale: return "stale";
    }
    return "unknown";
}

AuthoredQueryDispatcher::AuthoredQueryDispatcher(const AuthoredQueryRegistry& registry)
    : Queries(registry)
    , Link(std::make_shared<AuthoredQueryBindingToken::Link>())
{
    Link->Target = this;
}

AuthoredQueryDispatcher::~AuthoredQueryDispatcher()
{
    // Tokens outliving this become inert rather than dangling.
    Link->Target = nullptr;
}

const AuthoredQueryDispatcher::Implementation* AuthoredQueryDispatcher::Find(
    AuthoredQueryId query) const
{
    if (!query.IsValid() || AuthoredQueryRegistry::IndexOf(query) >= Implementations.size())
        return nullptr;
    const Implementation& entry = Implementations[AuthoredQueryRegistry::IndexOf(query)];
    return entry.Evaluate != nullptr ? &entry : nullptr;
}

AuthoredQueryBindingToken AuthoredQueryDispatcher::BindErased(AuthoredQueryId query,
                                                              const void* target,
                                                              EvaluateFn evaluate)
{
    assert(Evaluating == 0 && "a query cannot be bound while one is being answered");
    if (Evaluating != 0 || !Queries.IsLive(query) || target == nullptr || evaluate == nullptr)
        return {};

    const std::size_t slot = AuthoredQueryRegistry::IndexOf(query);
    if (slot >= Implementations.size())
        Implementations.resize(slot + 1);

    Implementation& entry = Implementations[slot];
    entry.Target = target;
    entry.Evaluate = evaluate;
    entry.Generation = AuthoredQueryBindingGeneration{ ++NextGeneration };
    entry.Revision = Queries.Revision(query);
    return AuthoredQueryBindingToken(Link, query, entry.Generation);
}

void AuthoredQueryDispatcher::Release(AuthoredQueryId query,
                                      AuthoredQueryBindingGeneration generation)
{
    assert(Evaluating == 0 && "a query cannot be unbound while one is being answered");
    if (!query.IsValid() || AuthoredQueryRegistry::IndexOf(query) >= Implementations.size())
        return;
    Implementation& entry = Implementations[AuthoredQueryRegistry::IndexOf(query)];
    // Only the binding this token minted. A replacement bound since belongs to
    // someone else.
    if (entry.Generation != generation)
        return;
    entry = Implementation{};
}

bool AuthoredQueryDispatcher::HasImplementation(AuthoredQueryId query) const
{
    return Find(query) != nullptr;
}

AuthoredQueryStatus AuthoredQueryDispatcher::Evaluate(AuthoredQueryId query,
                                                      std::span<const AuthoredValue> arguments,
                                                      AuthoredValue& result,
                                                      AuthoredQueryRevision expected) const
{
    const AuthoredQueryDefinition* definition = Queries.Get(query);
    if (definition == nullptr)
        return AuthoredQueryStatus::Stale;
    const AuthoredQueryRevision current = Queries.Revision(query);
    if (expected.IsValid() && expected != current)
        return AuthoredQueryStatus::Stale;

    const Implementation* implementation = Find(query);
    if (implementation == nullptr || implementation->Revision != current)
        return AuthoredQueryStatus::Unbound;

    // Checked here, once, for every implementation: the adapter decodes what
    // it is handed, but a range or a choice is the declaration's promise, and
    // the declaration lives here.
    const std::vector<DataFieldSchema>& declared = definition->Arguments.Children;
    if (arguments.size() != declared.size())
        return AuthoredQueryStatus::InvalidArguments;
    for (std::size_t index = 0; index < declared.size(); ++index)
    {
        if (!AuthoredValueSatisfiesField(arguments[index], declared[index]))
            return AuthoredQueryStatus::InvalidArguments;
    }

    ++Evaluating;
    const AuthoredQueryStatus status =
        implementation->Evaluate(implementation->Target, arguments, result);
    --Evaluating;
    return status;
}

std::vector<AuthoredQueryId> AuthoredQueryDispatcher::Unanswered() const
{
    std::vector<AuthoredQueryId> unanswered;
    for (const AuthoredQueryId query : Queries.Live())
    {
        const Implementation* implementation = Find(query);
        if (implementation == nullptr || implementation->Revision != Queries.Revision(query))
            unanswered.push_back(query);
    }
    return unanswered;
}
