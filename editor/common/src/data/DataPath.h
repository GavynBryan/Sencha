#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct DataFieldSchema;

// A JSON path into a data document's "data" object, as validation reports and
// properties are keyed: "$.data.contexts[0].bindings[2].control".
inline constexpr std::string_view kDataPathRoot = "$.data";

enum class DataPathStepKind : std::uint8_t
{
    Member,
    Index,
    Keyed,
};

struct DataPathStep
{
    DataPathStepKind Kind = DataPathStepKind::Member;
    std::string Key;
    std::size_t Index = 0;
};

// The steps after the root; nothing when the path does not start at it or
// does not parse.
[[nodiscard]] std::optional<std::vector<DataPathStep>> ParseDataPath(std::string_view path);
// The path without its last step: "$.data.a[2].b" is under "$.data.a[2]";
// nothing for the root or a path that does not parse.
[[nodiscard]] std::optional<std::string_view> DataPathParent(std::string_view path);
// The index the path's last step names, or nothing when it ends at a member.
[[nodiscard]] std::optional<std::size_t> DataPathLastIndex(std::string_view path);
[[nodiscard]] JsonValue* FindDataValue(JsonValue& data, std::string_view path);
[[nodiscard]] const JsonValue* FindDataValue(const JsonValue& data, std::string_view path);
[[nodiscard]] const JsonValue* FindDataValue(const JsonValue& data, const DataFieldSchema& root, std::string_view path);
[[nodiscard]] const JsonValue* FindKeyedElement(const JsonValue& list, const DataFieldSchema& field, std::string_view key);
[[nodiscard]] JsonValue* FindKeyedElement(JsonValue& list, const DataFieldSchema& field, std::string_view key);
