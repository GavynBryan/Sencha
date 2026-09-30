#pragma once

#include "properties/PropertyNode.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Properties for plain settings -- a tool's, a view's: one of several named
// options, a number in a range, a switch. Values travel as text both ways, the
// property model's rule; these write it and read it back.

[[nodiscard]] PropertyNode ChoiceProperty(std::string key, std::string label, std::span<const std::string_view> options,
                                          std::size_t current, std::string tooltip = {});
[[nodiscard]] PropertyNode NumberProperty(std::string key, std::string label, double value, double min, double max,
                                          double step, std::string tooltip = {});
[[nodiscard]] PropertyNode ToggleProperty(std::string key, std::string label, bool on, std::string tooltip = {});

[[nodiscard]] std::optional<std::size_t> ParseChoice(std::string_view value, std::span<const std::string_view> options);
// A number, clamped to the property's range when it has one.
[[nodiscard]] std::optional<double> ParseNumber(std::string_view value, double min = 0.0, double max = -1.0);
[[nodiscard]] std::optional<bool> ParseToggle(std::string_view value);
