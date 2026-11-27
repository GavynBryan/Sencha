#include <core/text/ValueText.h>

#include <algorithm>
#include <charconv>

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
