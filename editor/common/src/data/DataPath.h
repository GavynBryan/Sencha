#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A JSON path into a data document's "data" object, as validation reports and
// properties are keyed: "$.data.contexts[0].bindings[2].control".
inline constexpr std::string_view kDataPathRoot = "$.data";

struct DataPathStep
{
    // A member's key, or empty for an array index.
    std::string Key;
    std::optional<std::size_t> Index;
};

// The steps after the root; nothing when the path does not start at it or
// does not parse.
[[nodiscard]] std::optional<std::vector<DataPathStep>> ParseDataPath(std::string_view path);
// The value at `path` under the data object, or null when nothing is there.
[[nodiscard]] JsonValue* FindDataValue(JsonValue& data, std::string_view path);
[[nodiscard]] const JsonValue* FindDataValue(const JsonValue& data, std::string_view path);
