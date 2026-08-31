#pragma once

#include "scene_source/Json5Value.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Json5ParseError
{
    std::size_t Line = 0;   // 1-based
    std::size_t Column = 0; // 1-based, in bytes
    std::string Message;
};

// Parses one JSON5 document: a single value with comments captured as trivia
// on the values they precede. Deviations from the JSON5 1.0 spec, all chosen
// for authored scene content and reported with actionable errors:
//
//   - Non-finite numbers (Infinity, NaN) are recognized and rejected: a scene
//     with a non-finite value in it is a defect, not data.
//   - Duplicate object keys are rejected rather than last-wins: in authored
//     content a duplicate is a merge accident about to lose someone's work.
//   - Unquoted keys are ASCII identifiers ([A-Za-z_$][A-Za-z0-9_$]*); a
//     non-ASCII key must be quoted. The writer quotes such keys itself.
//
// `endComments` receives any comments after the document's closing bracket
// (a trailing file comment), so a full-fidelity caller can round-trip them.
[[nodiscard]] std::optional<Json5Value> Json5Parse(
    std::string_view text,
    Json5ParseError* error = nullptr,
    std::vector<std::string>* endComments = nullptr);
