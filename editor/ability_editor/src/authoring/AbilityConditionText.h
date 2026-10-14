#pragma once

#include <core/json/JsonValue.h>

#include <string>

// A condition as a short phrase, locals by their names: "Hit struck",
// "phase ticks \xE2\x89\xA5 6", "Energy > 0", "has Weapon.CTX.Mode.Charge".
[[nodiscard]] std::string AbilityConditionText(const JsonValue& data, const JsonValue& condition);
