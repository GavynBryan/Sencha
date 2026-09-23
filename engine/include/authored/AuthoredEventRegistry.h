#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredEventId.h>
#include <authored/AuthoredSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// AuthoredEventRegistry
//
// One World's authored events: which occurrences gameplay code announces, what
// each carries, and which entities are expected to announce it. Metadata only
// -- an editor installs this to offer "West Torch: Lit" as a trigger without
// constructing anything that could publish or deliver one.
//
// An event is something that happened, stated by the code that made it
// happen. It is never derived from a query or from watching component
// memory; this catalog lists the announcements gameplay code makes, and
// nothing more.
//=============================================================================

struct AuthoredEventDefinition
{
    // Exact, case-sensitive, dot-separated: "torch.lit".
    std::string Name;

    std::string DisplayName;
    std::string Description;
    std::string Category;

    // The component a source entity is expected to carry, by its persisted
    // identity; empty when any entity may be a source. Authoring metadata:
    // it lets a graph offer the event on the entities that have one, and
    // publishing does not enforce it.
    std::string SourceComponent;

    // A record root, one child per payload member, in the order the payload
    // arguments hold them. The source entity travels beside the payload, not
    // inside it.
    DataFieldSchema Payload = AuthoredRecordRoot();
};

// What makes a catalog an event catalog.
struct AuthoredEventCatalogTraits
{
    using Definition = AuthoredEventDefinition;
    using Id = AuthoredEventId;
    using Revision = AuthoredEventRevision;
    using CatalogId = AuthoredEventCatalogId;
    static constexpr std::string_view Noun = "event";

    static void Validate(const AuthoredEventDefinition& definition,
                         std::vector<std::string>& errors);

    // The payload is the contract; the expected source is metadata a provider
    // may refine without invalidating a subscriber.
    static bool ContractsMatch(const AuthoredEventDefinition& left,
                               const AuthoredEventDefinition& right)
    {
        return AuthoredContractsMatch(left.Payload, right.Payload);
    }
};

using AuthoredEventRegistry = AuthoredCatalog<AuthoredEventCatalogTraits>;
using AuthoredEventRegistrationScope = AuthoredRegistrationScope<AuthoredEventCatalogTraits>;
