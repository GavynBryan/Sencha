#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

// The authored `require` block: a number admitted when it lies in [Min, Max].
// A true or false answer reads as 1 or 0, so {expect} is [1, 1] or [0, 0].
struct ValueBounds
{
    double Min = -std::numeric_limits<double>::infinity();
    double Max = std::numeric_limits<double>::infinity();

    [[nodiscard]] bool Admits(double value) const { return value >= Min && value <= Max; }

    [[nodiscard]] static ValueBounds Exactly(double value) { return { value, value }; }
};

// Reads {min, max} or {expect}; a bound left out is open.
[[nodiscard]] bool ReadValueBounds(const JsonValue& require, ValueBounds& out, std::string_view subject,
                                   std::vector<std::string>& errors);

[[nodiscard]] DataFieldSchema MakeValueBoundsField(std::string key, std::string display);
