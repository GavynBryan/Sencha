#pragma once

#include "problems/ProblemList.h"
#include "vocabulary/VocabularyCatalog.h"
#include "workspaces/TreeOutline.h"
#include "workspaces/facets/ExplorerFacet.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

// A structured-data asset whose value is a binding set, each record inspected
// against the catalog.
struct BindingAsset
{
    std::string Path;
    std::vector<VocabularyCatalog::BindingRow> Records;
};

// The vocabulary a project's documents author against, as an outline: verbs,
// queries and events, and every binding asset with its records.
class VocabularyOutline
{
public:
    [[nodiscard]] ExplorerPage Explore(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings,
                                       const ExplorerQuery& query);
    // Opens or closes the branch `key` names; false when it names an entry.
    bool Flip(std::string_view key);

private:
    TreeOutline Outline;
};

// What is wrong with the vocabulary: the module's declarations that failed,
// and binding records that do not resolve or whose references went unchecked.
[[nodiscard]] std::vector<Problem> VocabularyProblems(const VocabularyCatalog& catalog,
                                                      std::span<const BindingAsset> bindings);
