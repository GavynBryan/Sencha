#pragma once

#include <cstddef>
#include <iosfwd>

class EditorOperations;

// Runs one operation per line of `script`, its name and then its arguments as
// OperationArgumentsFromText reads them, writing each answer to `answers` as
// one line of JSON in the same order. Blank lines and lines starting with '#'
// are skipped. Returns how many answers had the status "error".
std::size_t RunOperationScript(std::istream& script, std::ostream& answers,
                               EditorOperations& operations);
