#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string_view>

// A verb or query argument's constant as a person types it, in the verb-binding wrapper ({const}, {tag}, ...).

// The field an optional argument wraps, or the field itself.
[[nodiscard]] const DataFieldSchema& UnwrappedField(const DataFieldSchema& field);

// The constant `text` spells for `field`'s kind; none when it spells no such value.
[[nodiscard]] std::optional<JsonValue> AuthoredConstantFromText(const DataFieldSchema& field, std::string_view text);
