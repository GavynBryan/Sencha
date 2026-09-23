#pragma once

#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

// Naming and schema rules shared by the verb, query and event catalogs.

[[nodiscard]] inline DataFieldSchema AuthoredRecordRoot()
{
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    return root;
}

// Dot-separated identifier segments, stored exactly as declared.
[[nodiscard]] bool IsValidAuthoredName(std::string_view name);

// One message per problem, each prefixed with the member's path.
void ValidateAuthoredField(const DataFieldSchema& field,
                           const std::string& path,
                           std::vector<std::string>& errors);

// Compares what a value must satisfy; ignores presentation and the expected
// target component.
[[nodiscard]] bool AuthoredContractsMatch(const DataFieldSchema& left,
                                          const DataFieldSchema& right);
