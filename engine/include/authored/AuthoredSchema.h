#pragma once

#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Authored contract rules shared by every catalog
//
// Verbs, queries and events are named and shaped the same way: a dotted name
// content persists, and a DataFieldSchema describing the values that cross the
// authored boundary. These are the rules for both, stated once so the three
// catalogs cannot drift into three dialects of what a valid argument is.
//=============================================================================

// A record with no members: the root every argument list and payload starts
// from. A declaration that forgot to say "record" is a mistake with no upside.
[[nodiscard]] inline DataFieldSchema AuthoredRecordRoot()
{
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    return root;
}

// Whether a name is spelled the way a catalog persists names. Dot-separated
// ASCII segments, each starting with a letter or underscore and continuing
// with letters, digits or underscores. No empty segment, no surrounding
// whitespace, and no normalization: a name is stored exactly as declared.
[[nodiscard]] bool IsValidAuthoredName(std::string_view name);

// Appends one message per problem in `field` and everything beneath it, each
// prefixed with `path` so an author can find the member that is wrong.
void ValidateAuthoredField(const DataFieldSchema& field,
                           const std::string& path,
                           std::vector<std::string>& errors);

// Whether two declarations mean the same contract. Structure, keys, kinds,
// constraints, enum values, optionality and nesting -- not display names,
// summaries, descriptions, units, editor hints or the component a target is
// expected to carry, which a provider may reword without invalidating a
// binding compiled last week.
[[nodiscard]] bool AuthoredContractsMatch(const DataFieldSchema& left,
                                          const DataFieldSchema& right);
