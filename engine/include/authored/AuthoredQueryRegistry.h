#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredQueryId.h>
#include <authored/AuthoredSchema.h>

#include <string>
#include <string_view>
#include <vector>

// One World's authored queries. Metadata only.

struct AuthoredQueryDefinition
{
    // e.g. "inventory.has_item"
    std::string Name;

    std::string DisplayName;
    std::string Description;
    std::string Category;

    DataFieldSchema Arguments = AuthoredRecordRoot();

    // Never Optional: no answer is Unavailable, not "absent".
    DataFieldSchema Result;
};

struct AuthoredQueryCatalogTraits
{
    using Definition = AuthoredQueryDefinition;
    using Id = AuthoredQueryId;
    using Revision = AuthoredQueryRevision;
    using CatalogId = AuthoredQueryCatalogId;
    static constexpr std::string_view Noun = "query";

    static void Validate(const AuthoredQueryDefinition& definition,
                         std::vector<std::string>& errors);

    static bool ContractsMatch(const AuthoredQueryDefinition& left,
                               const AuthoredQueryDefinition& right)
    {
        return AuthoredContractsMatch(left.Arguments, right.Arguments)
            && AuthoredContractsMatch(left.Result, right.Result);
    }
};

using AuthoredQueryRegistry = AuthoredCatalog<AuthoredQueryCatalogTraits>;
using AuthoredQueryRegistrationScope = AuthoredRegistrationScope<AuthoredQueryCatalogTraits>;
