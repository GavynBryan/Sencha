#pragma once

#include <core/json/JsonValue.h>

#include <string>

// Deterministic source formatter for authored JSON. Object order follows the
// JsonValue DOM, which preserves insertion order.
[[nodiscard]] std::string JsonFormat(const JsonValue& value, int indentSpaces = 2);
