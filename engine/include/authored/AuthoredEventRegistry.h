#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredEventId.h>
#include <authored/AuthoredSchema.h>

#include <string>
#include <string_view>
#include <vector>

// One World's authored events. Metadata only.

struct AuthoredEventDefinition
{
    // e.g. "torch.lit"
    std::string Name;

    std::string DisplayName;
    std::string Description;
    std::string Category;

    // Authoring metadata; publishing does not enforce it. Empty for any source.
    std::string SourceComponent;

    // The source entity travels beside the payload, not in it.
    DataFieldSchema Payload = AuthoredRecordRoot();
};

struct AuthoredEventCatalogTraits
{
    using Definition = AuthoredEventDefinition;
    using Id = AuthoredEventId;
    using Revision = AuthoredEventRevision;
    using CatalogId = AuthoredEventCatalogId;
    static constexpr std::string_view Noun = "event";

    static void Validate(const AuthoredEventDefinition& definition,
                         std::vector<std::string>& errors);

    static bool ContractsMatch(const AuthoredEventDefinition& left,
                               const AuthoredEventDefinition& right)
    {
        return AuthoredContractsMatch(left.Payload, right.Payload);
    }
};

using AuthoredEventRegistry = AuthoredCatalog<AuthoredEventCatalogTraits>;
using AuthoredEventRegistrationScope = AuthoredRegistrationScope<AuthoredEventCatalogTraits>;
