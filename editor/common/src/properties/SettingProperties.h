#pragma once

#include "properties/PropertyNode.h"

#include <core/text/ValueText.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Properties for plain settings -- a tool's, a view's: one of several named
// options, a number in a range, a switch. Values travel as text both ways, the
// property model's rule; these write it and read it back.

[[nodiscard]] PropertyNode GroupProperty(std::string key, std::string label, std::vector<PropertyNode> children = {});
[[nodiscard]] PropertyNode TextProperty(std::string key, std::string label, std::string value, std::string tooltip = {});
// A fact shown beside the settings, never edited.
[[nodiscard]] PropertyNode NoteProperty(std::string key, std::string label, std::string text);
[[nodiscard]] PropertyNode ChoiceProperty(std::string key, std::string label, std::span<const std::string_view> options,
                                          std::size_t current, std::string tooltip = {});
[[nodiscard]] PropertyNode NumberProperty(std::string key, std::string label, double value, double min, double max,
                                          double step, std::string tooltip = {});
[[nodiscard]] PropertyNode ToggleProperty(std::string key, std::string label, bool on, std::string tooltip = {});
// Several numbers edited together, written "x, y".
[[nodiscard]] PropertyNode VectorProperty(std::string key, std::string label, std::span<const double> values,
                                          std::string tooltip = {});

[[nodiscard]] std::optional<std::size_t> ParseChoice(std::string_view value, std::span<const std::string_view> options);
// A number as properties write it.
[[nodiscard]] std::string NumberText(double value);
[[nodiscard]] std::string JoinVector(std::span<const std::string> parts);
