#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string>
#include <string_view>

// A verb or query argument's constant as a person types and reads it, for any
// editor that lays arguments out from their contract. The JSON is the
// verb-binding wrapper ({const}, {tag}, {data}, {asset}); an entity is never a
// typed constant.

// The field an optional argument wraps, or the field itself.
[[nodiscard]] const DataFieldSchema& UnwrappedField(const DataFieldSchema& field);

[[nodiscard]] std::string AuthoredConstantText(const JsonValue& argument);

// The constant `text` spells for `field`'s kind; none when it spells no such value.
[[nodiscard]] std::optional<JsonValue> AuthoredConstantFromText(const DataFieldSchema& field, std::string_view text);

// What a newly chosen constant argument starts at.
[[nodiscard]] JsonValue DefaultAuthoredConstant(const DataFieldSchema& field);
