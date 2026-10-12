#include "properties/SettingProperties.h"

#include <algorithm>
#include <charconv>
#include <format>

PropertyNode GroupProperty(std::string key, std::string label, std::vector<PropertyNode> children)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Group;
    node.Descriptor.Label = std::move(label);
    node.Children = std::move(children);
    return node;
}

PropertyNode TextProperty(std::string key, std::string label, std::string value, std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Text;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    node.Value = std::move(value);
    return node;
}

PropertyNode NoteProperty(std::string key, std::string label, std::string text)
{
    PropertyNode node = TextProperty(std::move(key), std::move(label), std::move(text));
    node.Descriptor.ReadOnly = true;
    return node;
}

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

PropertyNode VectorProperty(std::string key, std::string label, std::span<const double> values, std::string tooltip)
{
    PropertyNode node;
    node.Key = std::move(key);
    node.Descriptor.Kind = PropertyKind::Vector;
    node.Descriptor.Label = std::move(label);
    node.Descriptor.Tooltip = std::move(tooltip);
    std::vector<std::string> parts;
    for (const double value : values)
        parts.push_back(NumberText(value));
    node.Value = JoinVector(parts);
    return node;
}

std::optional<std::size_t> ParseChoice(std::string_view value, std::span<const std::string_view> options)
{
    const auto it = std::ranges::find(options, value);
    if (it == options.end())
        return std::nullopt;
    return static_cast<std::size_t>(it - options.begin());
}

namespace
{
std::string_view Trimmed(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    return text;
}
} // namespace

std::optional<double> ParseNumber(std::string_view value, double min, double max)
{
    value = Trimmed(value);
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

std::string NumberText(double value)
{
    return std::format("{:.6g}", value);
}

std::vector<std::string> VectorParts(std::string_view value)
{
    std::vector<std::string> parts;
    while (!value.empty())
    {
        const std::size_t comma = value.find(',');
        parts.emplace_back(Trimmed(value.substr(0, comma)));
        if (comma == std::string_view::npos)
            break;
        value.remove_prefix(comma + 1);
    }
    return parts;
}

std::string JoinVector(std::span<const std::string> parts)
{
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i)
        joined += (i == 0 ? "" : ", ") + parts[i];
    return joined;
}

std::optional<std::vector<double>> ParseVector(std::string_view value, std::size_t count)
{
    std::vector<double> numbers;
    for (const std::string& part : VectorParts(value))
    {
        const std::optional<double> number = ParseNumber(part);
        if (!number.has_value())
            return std::nullopt;
        numbers.push_back(*number);
    }
    if (numbers.size() != count)
        return std::nullopt;
    return numbers;
}
