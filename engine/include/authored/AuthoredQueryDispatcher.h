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

//=============================================================================
// AuthoredQueryDispatcher
//
// Where an authored consumer asks a question and the object that owns the
// answer replies. The query counterpart of VerbDispatcher, with the same
// shape: a table of {target, adapter} rows indexed by the query's dense slot,
// no switch, no subsystem list, nothing a new query adds but a row.
//
// Synchronous and observational. An implementation is handed a const target
// and answers now; it cannot defer, and it has no path to a write. A query
// asked while another is being answered is ordinary -- one question may be
// built from others -- but binding or unbinding during an answer is refused,
// because the row being read would change under it.
//
// Composed by the runtime host, never discovered, and never installed in an
// editor's metadata World. Owner-thread only.
//=============================================================================
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

    // Puts a generated adapter behind one query, against the query's current
    // revision. `Evaluate` is called with the target it was bound with. An
    // unknown query, or a call made while answering, returns an invalid token
    // and binds nothing. Binding over an existing implementation replaces it
    // and moves the generation, so the previous owner's token can no longer
    // remove it.
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

    // Asks one question, by a handle resolved against this dispatcher's
    // catalog. A handle from another catalog, or for a contract that has moved
    // since it was resolved, is Stale.
    //
    // `arguments` are every argument the query declares, in declaration order:
    // a declared default is for whatever compiles the call to fill in, so
    // evaluating stays a check and a call, and an omitted argument is
    // InvalidArguments. They are checked against the declaration before the
    // implementation runs, and its answer is checked against the declared
    // result before it is handed on: Value means a value that satisfies the
    // contract. `result` is written only then.
    [[nodiscard]] AuthoredQueryStatus Evaluate(const AuthoredQueryHandle& query,
                                               std::span<const AuthoredValue> arguments,
                                               AuthoredValue& result) const;

    // Live queries nothing answers, in id order. What a host reports once
    // everything that binds has had its chance.
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
        // The contract the implementation was written against.
        AuthoredQueryRevision Revision;
    };

    [[nodiscard]] AuthoredQueryBindingToken BindErased(AuthoredQueryId query,
                                                       const void* target,
                                                       EvaluateFn evaluate);
    void Release(AuthoredQueryId query, AuthoredQueryBindingGeneration generation);

    [[nodiscard]] const Implementation* Find(AuthoredQueryId query) const;

    const AuthoredQueryRegistry& Queries;
    std::shared_ptr<AuthoredQueryBindingToken::Link> Link;

    // Indexed by the query's dense slot, grown as queries acquire
    // implementations.
    std::vector<Implementation> Implementations;
    std::uint32_t NextGeneration = 0;

    // Depth, not a flag: a query may be built from others.
    mutable std::uint32_t Evaluating = 0;
};
