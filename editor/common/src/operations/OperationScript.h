#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string_view>

#include <core/json/JsonValue.h>

class EditorOperations;

// The answer to one line: its operation's name and then its arguments as
// OperationArgumentsFromText reads them. Nullopt for a blank line or one
// starting with '#'.
[[nodiscard]] std::optional<JsonValue> AnswerOperationLine(std::string_view line, EditorOperations& operations);

// Runs one operation per line of `script`, its name and then its arguments as
// OperationArgumentsFromText reads them, writing each answer to `answers` as
// one line of JSON in the same order. Blank lines and lines starting with '#'
// are skipped. Returns how many answers had the status "error".
std::size_t RunOperationScript(std::istream& script, std::ostream& answers,
                               EditorOperations& operations);
