#pragma once

#include "scene_source/Json5Value.h"

#include <span>
#include <string>

// Renders a Json5Value in the one canonical style scene source is written in:
// two-space indent, unquoted ASCII-identifier keys, double-quoted strings,
// trailing commas in multiline containers, short scalar arrays on one line.
// Comments are re-indented but never edited. The style is deterministic, which
// is the property the save-twice test pins: write(parse(write(x))) == write(x).
//
// `endComments` render after the closing bracket (a trailing file comment).
[[nodiscard]] std::string Json5Write(const Json5Value& root,
                                     std::span<const std::string> endComments = {});
