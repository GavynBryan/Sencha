#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredClassification.h>
#include <authored/AuthoredSchema.h>
#include <authored/VerbId.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// VerbRegistry
//
// One World's authored vocabulary: which semantic operations content may name,
// and what each one's arguments are. A World resource, because the names
// content resolves against are a property of the entity universe it was loaded
// into -- two editor documents are two catalogs, and the same number means a
// different verb in each.
//
// Metadata only. What a verb *does* is a registered implementation held by the
// dispatcher, which is a separate object with a separate lifetime: an editor
// installs this to offer and validate a vocabulary without acquiring the power
// to run any of it.
//
//=============================================================================

// An argument root with no arguments in it. Most verbs take none, and a
// declaration that forgot to say "record" is a mistake with no upside.
[[nodiscard]] inline DataFieldSchema EmptyVerbArguments()
{
    return AuthoredRecordRoot();
}

// An instant verb runs once per request. A lease is acquired for an owner and
// held until that owner releases it or its entity ends.
enum class VerbKind : std::uint8_t
{
    Instant,
    Lease,
};

// What a verb is called, what it takes, and how an authoring surface should
// say it. The name is the persisted contract; everything else but the argument
// schema, kind and classification is presentation.
struct VerbDefinition
{
    // Exact, case-sensitive, dot-separated: "runtime.resume". A prefix
    // organizes a picker and is never parsed to select a subsystem.
    std::string Name;

    std::string DisplayName;
    std::string Description;

    // Optional presentation grouping. Not an identity and not a namespace.
    std::string Category;

    // A record root, one child per named argument. Reusing DataFieldSchema is
    // what keeps ranges, enum choices, optionality and nesting described once
    // for the inspector, the binding compiler, and the .sdata validator.
    DataFieldSchema Arguments = EmptyVerbArguments();

    VerbKind Kind = VerbKind::Instant;
    AuthoredScope Scope = AuthoredScope::Gameplay;
    AuthoredPrediction Prediction = AuthoredPrediction::AuthorityOnly;
    // A lease whose hold can finish while still held, as a timed burst does; only
    // such a lease's provider is asked (LeaseStep::Probe) whether a hold goes on.
    bool Completes = false;
};

struct VerbCatalogTraits
{
    using Definition = VerbDefinition;
    using Id = VerbId;
    using Revision = VerbContractRevision;
    using CatalogId = VerbCatalogId;
    static constexpr std::string_view Noun = "verb";

    static void Validate(const VerbDefinition& definition, std::vector<std::string>& errors);

    static bool ContractsMatch(const VerbDefinition& left, const VerbDefinition& right)
    {
        return left.Kind == right.Kind && left.Scope == right.Scope && left.Prediction == right.Prediction
            && left.Completes == right.Completes && AuthoredContractsMatch(left.Arguments, right.Arguments);
    }
};

using VerbRegistry = AuthoredCatalog<VerbCatalogTraits>;
using VerbRegistrationScope = AuthoredRegistrationScope<VerbCatalogTraits>;
