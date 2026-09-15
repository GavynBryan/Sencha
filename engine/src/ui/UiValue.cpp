#include <ui/UiValue.h>

bool UiValue::AsBool(bool fallback) const
{
    const bool* value = std::get_if<bool>(&Storage);
    return value != nullptr ? *value : fallback;
}

std::int64_t UiValue::AsInt(std::int64_t fallback) const
{
    const std::int64_t* value = std::get_if<std::int64_t>(&Storage);
    return value != nullptr ? *value : fallback;
}

double UiValue::AsFloat(double fallback) const
{
    if (const double* value = std::get_if<double>(&Storage))
        return *value;
    // An integer read as a float is the one widening worth doing: a host
    // publishing a count that a document formats as a number should not have to
    // know which the property was declared as.
    if (const std::int64_t* value = std::get_if<std::int64_t>(&Storage))
        return static_cast<double>(*value);
    return fallback;
}

std::string_view UiValue::AsString(std::string_view fallback) const
{
    const std::string* value = std::get_if<std::string>(&Storage);
    return value != nullptr ? std::string_view(*value) : fallback;
}

std::uint64_t UiValue::AsId(std::uint64_t fallback) const
{
    const Identity* value = std::get_if<Identity>(&Storage);
    return value != nullptr ? value->Value : fallback;
}
