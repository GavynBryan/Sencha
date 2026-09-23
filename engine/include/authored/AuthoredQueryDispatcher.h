#pragma once

#include <authored/AuthoredBindingToken.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/AuthoredQueryStatus.h>
#include <authored/AuthoredValue.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

class AuthoredQueryDispatcher;

using AuthoredQueryBindingToken =
    AuthoredBindingToken<AuthoredQueryDispatcher, AuthoredQueryId, AuthoredQueryBindingGeneration>;

// Synchronous, read-only answers to authored queries: a table of {const
// target, adapter} rows indexed by query slot. Queries may nest; binding or
// unbinding while one is answered is refused. Owner-thread only.
class AuthoredQueryDispatcher
{
public:
    explicit AuthoredQueryDispatcher(const AuthoredQueryRegistry& registry);
    ~AuthoredQueryDispatcher();

    AuthoredQueryDispatcher(const AuthoredQueryDispatcher&) = delete;
    AuthoredQueryDispatcher& operator=(const AuthoredQueryDispatcher&) = delete;
    AuthoredQueryDispatcher(AuthoredQueryDispatcher&&) = delete;
    AuthoredQueryDispatcher& operator=(AuthoredQueryDispatcher&&) = delete;

    [[nodiscard]] const AuthoredQueryRegistry& Registry() const { return Queries; }

    // Binds against the query's current revision. Rebinding replaces the
    // previous implementation, whose token then no longer removes anything.
    template<auto Evaluate, typename T>
        requires std::is_invocable_r_v<AuthoredQueryStatus, decltype(Evaluate), const T&,
                                       std::span<const AuthoredValue>, AuthoredValue&>
    [[nodiscard]] AuthoredQueryBindingToken Bind(AuthoredQueryId query, const T& target)
    {
        return BindErased(query, &target,
                          [](const void* self, std::span<const AuthoredValue> arguments,
                             AuthoredValue& result) {
                              return Evaluate(*static_cast<const T*>(self), arguments, result);
                          });
    }

    [[nodiscard]] bool HasImplementation(AuthoredQueryId query) const;

    // `arguments` holds every declared argument in order; declared defaults
    // are filled by whoever compiles the call. Arguments and the answer are
    // both checked against the declaration.
    [[nodiscard]] AuthoredQueryStatus Evaluate(const AuthoredQueryHandle& query,
                                               std::span<const AuthoredValue> arguments,
                                               AuthoredValue& result) const;

    // Live queries with no current implementation, in id order.
    [[nodiscard]] std::vector<AuthoredQueryId> Unanswered() const;

    [[nodiscard]] bool IsEvaluating() const { return Evaluating != 0; }

private:
    friend AuthoredQueryBindingToken;

    using EvaluateFn =
        AuthoredQueryStatus (*)(const void*, std::span<const AuthoredValue>, AuthoredValue&);

    struct Implementation
    {
        const void* Target = nullptr;
        EvaluateFn Evaluate = nullptr;
        AuthoredQueryBindingGeneration Generation;
        AuthoredQueryRevision Revision;
    };

    [[nodiscard]] AuthoredQueryBindingToken BindErased(AuthoredQueryId query,
                                                       const void* target,
                                                       EvaluateFn evaluate);
    void Release(AuthoredQueryId query, AuthoredQueryBindingGeneration generation);

    [[nodiscard]] const Implementation* Find(AuthoredQueryId query) const;

    const AuthoredQueryRegistry& Queries;
    std::shared_ptr<AuthoredQueryBindingToken::Link> Link;

    // Indexed by query slot.
    std::vector<Implementation> Implementations;
    std::uint32_t NextGeneration = 0;

    // A depth: queries may nest.
    mutable std::uint32_t Evaluating = 0;
};
