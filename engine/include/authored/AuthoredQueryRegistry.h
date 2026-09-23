#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredQueryId.h>
#include <authored/AuthoredSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// AuthoredQueryRegistry
//
// One World's authored questions: what content may ask about the simulation,
// what each question takes, and what shape its answer has. Metadata only, like
// the verb catalog beside it -- an editor installs this to offer "Torch: Lit"
// in a condition picker without acquiring anything that could evaluate it.
//
// Queries are distinct from verbs rather than a kind of verb. A query answers
// synchronously and never changes simulation state; a verb is admitted and
// may run later. Sharing a catalog would mean every consumer re-asking which
// one it had.
//=============================================================================

struct AuthoredQueryDefinition
{
    // Exact, case-sensitive, dot-separated: "inventory.has_item". A prefix
    // organizes a picker and is never parsed to select a subsystem.
    std::string Name;

    std::string DisplayName;
    std::string Description;

    // Optional presentation grouping. Not an identity and not a namespace.
    std::string Category;

    // A record root, one child per named argument, in the order an evaluator
    // supplies them.
    DataFieldSchema Arguments = AuthoredRecordRoot();

    // What a Value answer holds. Never an optional: a query with no answer for
    // this entity says Unavailable rather than answering "absent", so a
    // condition can tell "false" from "does not apply".
    DataFieldSchema Result;
};

// What makes a catalog a query catalog.
struct AuthoredQueryCatalogTraits
{
    using Definition = AuthoredQueryDefinition;
    using Id = AuthoredQueryId;
    using Revision = AuthoredQueryRevision;
    using CatalogId = AuthoredQueryCatalogId;
    static constexpr std::string_view Noun = "query";

    static void Validate(const AuthoredQueryDefinition& definition,
                         std::vector<std::string>& errors);

    // The arguments and the result are both the contract: a caller compiled
    // against one shape of answer cannot read another.
    static bool ContractsMatch(const AuthoredQueryDefinition& left,
                               const AuthoredQueryDefinition& right)
    {
        return AuthoredContractsMatch(left.Arguments, right.Arguments)
            && AuthoredContractsMatch(left.Result, right.Result);
    }
};

using AuthoredQueryRegistry = AuthoredCatalog<AuthoredQueryCatalogTraits>;
using AuthoredQueryRegistrationScope = AuthoredRegistrationScope<AuthoredQueryCatalogTraits>;
