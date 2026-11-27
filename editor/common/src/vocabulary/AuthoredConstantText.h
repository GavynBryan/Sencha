#pragma once

#include <authored/AuthoredConstantText.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <optional>
#include <string>
#include <string_view>

// A verb or query argument's constant as a person reads it; authored/AuthoredConstantText.h reads it back.

[[nodiscard]] std::string AuthoredConstantText(const JsonValue& argument);

// What a newly chosen constant argument starts at.
[[nodiscard]] JsonValue DefaultAuthoredConstant(const DataFieldSchema& field);
