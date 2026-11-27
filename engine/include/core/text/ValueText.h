#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Numbers, switches and vectors as a person types them, read back exactly.

// A number, spaces around it allowed, clamped to the range when there is one.
[[nodiscard]] std::optional<double> ParseNumber(std::string_view value, double min = 0.0, double max = -1.0);
// "true" or "1", "false" or "0".
[[nodiscard]] std::optional<bool> ParseToggle(std::string_view value);
// A vector's numbers as written, each without the spaces around it.
[[nodiscard]] std::vector<std::string> VectorParts(std::string_view value);
// Exactly `count` numbers, separated by commas.
[[nodiscard]] std::optional<std::vector<double>> ParseVector(std::string_view value, std::size_t count);
