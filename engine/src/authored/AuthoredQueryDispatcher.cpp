#include <authored/AuthoredQueryDispatcher.h>

#include <cassert>
#include <utility>

const char* AuthoredQueryStatusName(AuthoredQueryStatus status)
{
    switch (status)
    {
    case AuthoredQueryStatus::Value: return "value";
    case AuthoredQueryStatus::Unavailable: return "unavailable";
    case AuthoredQueryStatus::InvalidArguments: return "invalid arguments";
    case AuthoredQueryStatus::Unbound: return "no implementation";
    case AuthoredQueryStatus::Stale: return "stale";
    case AuthoredQueryStatus::InvalidResult: return "invalid result";
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
    if (entry.Generation != generation)
        return;
    entry = Implementation{};
}

bool AuthoredQueryDispatcher::HasImplementation(AuthoredQueryId query) const
{
    return Find(query) != nullptr;
}

namespace
{
    // Restores the depth even when an implementation throws.
    class EvaluationScope
    {
    public:
        explicit EvaluationScope(std::uint32_t& depth) : Depth(depth) { ++Depth; }
        ~EvaluationScope() { --Depth; }
        EvaluationScope(const EvaluationScope&) = delete;
        EvaluationScope& operator=(const EvaluationScope&) = delete;

    private:
        std::uint32_t& Depth;
    };
}

AuthoredQueryStatus AuthoredQueryDispatcher::Evaluate(const AuthoredQueryHandle& query,
                                                      std::span<const AuthoredValue> arguments,
                                                      AuthoredValue& result) const
{
    if (!Queries.IsCurrent(query))
        return AuthoredQueryStatus::Stale;
    const AuthoredQueryDefinition& definition = *Queries.Get(query.Slot);

    const Implementation* implementation = Find(query.Slot);
    if (implementation == nullptr || implementation->Revision != query.Contract)
        return AuthoredQueryStatus::Unbound;

    const std::vector<DataFieldSchema>& declared = definition.Arguments.Children;
    if (arguments.size() != declared.size())
        return AuthoredQueryStatus::InvalidArguments;
    for (std::size_t index = 0; index < declared.size(); ++index)
    {
        if (!AuthoredValueSatisfiesField(arguments[index], declared[index]))
            return AuthoredQueryStatus::InvalidArguments;
    }

    // A local, so an answer that fails the check never reaches the caller.
    AuthoredValue answer;
    AuthoredQueryStatus status;
    {
        const EvaluationScope scope(Evaluating);
        status = implementation->Evaluate(implementation->Target, arguments, answer);
    }
    if (status != AuthoredQueryStatus::Value)
        return status;
    if (!AuthoredValueSatisfiesField(answer, definition.Result))
        return AuthoredQueryStatus::InvalidResult;
    result = std::move(answer);
    return AuthoredQueryStatus::Value;
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
