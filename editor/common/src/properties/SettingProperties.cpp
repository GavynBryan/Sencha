#include "properties/SettingProperties.h"

#include <algorithm>
#include <charconv>
#include <format>

PropertyNode ChoiceProperty(std::string key, std::string label, std::span<const std::string_view> options,
                            std::size_t current, std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Choice;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    for (const std::string_view option : options)
        node.Descriptor.Choices.emplace_back(option);
    node.Value = current < options.size() ? std::string(options[current]) : std::string{};
    return node;
}

PropertyNode NumberProperty(std::string key, std::string label, double value, double min, double max, double step,
                            std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Number;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    node.Descriptor.Min = min;
    node.Descriptor.Max = max;
    node.Descriptor.Step = step;
    node.Value = std::format("{}", value);
    return node;
}

PropertyNode ToggleProperty(std::string key, std::string label, bool on, std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Toggle;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    node.Value = on ? "true" : "false";
    return node;
}

std::optional<std::size_t> ParseChoice(std::string_view value, std::span<const std::string_view> options)
{
    const auto it = std::ranges::find(options, value);
    if (it == options.end())
        return std::nullopt;
    return static_cast<std::size_t>(it - options.begin());
}

std::optional<double> ParseNumber(std::string_view value, double min, double max)
{
    double parsed = 0.0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size())
        return std::nullopt;
    return max >= min ? std::clamp(parsed, min, max) : parsed;
}

std::optional<bool> ParseToggle(std::string_view value)
{
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    return std::nullopt;
}
