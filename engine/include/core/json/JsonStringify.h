#pragma once

#include <core/json/JsonValue.h>

#include <string>

[[nodiscard]] std::string JsonStringify(const JsonValue& value, bool pretty = false);
