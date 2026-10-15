#pragma once

#include <authored/AuthoredCatalog.h>
#include <authored/AuthoredSchema.h>
#include <core/identity/StrongId.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The named operations every editing client calls: Kyusu's tools, the console,
// the headless host and external agents. Metadata only; EditorOperations runs
// them. docs/plans/level-spatial-model.md, section 4.

enum class EditorOperationKind : std::uint8_t
{
    // Reads; changes nothing.
    Query,
    // Edits the document an operation client writes, so it is one undo step,
    // a draft records it, and a proposal may replay it.
    Mutation,
    // Changes the editor around the documents (drafts, focus, selection):
    // never a step of a draft or a proposal.
    Session,
};

struct EditorOperationDefinition
{
    // e.g. "query.near"
    std::string Name;

    std::string DisplayName;
    std::string Description;
    std::string Category;

    EditorOperationKind Kind = EditorOperationKind::Query;
    DataFieldSchema Parameters = AuthoredRecordRoot();
    DataFieldSchema Result = AuthoredRecordRoot();
};

using EditorOperationId = StrongId<struct EditorOperationTag, std::uint32_t>;
using EditorOperationCatalogId = StrongId<struct EditorOperationCatalogTag, std::uint64_t>;
using EditorOperationRevision = StrongId<struct EditorOperationRevisionTag, std::uint32_t>;

struct EditorOperationCatalogTraits
{
    using Definition = EditorOperationDefinition;
    using Id = EditorOperationId;
    using Revision = EditorOperationRevision;
    using CatalogId = EditorOperationCatalogId;
    static constexpr std::string_view Noun = "editor operation";

    static void Validate(const EditorOperationDefinition& definition,
                         std::vector<std::string>& errors);

    static bool ContractsMatch(const EditorOperationDefinition& left,
                               const EditorOperationDefinition& right)
    {
        return left.Kind == right.Kind
            && AuthoredContractsMatch(left.Parameters, right.Parameters)
            && AuthoredContractsMatch(left.Result, right.Result);
    }
};

using EditorOperationCatalog = AuthoredCatalog<EditorOperationCatalogTraits>;
