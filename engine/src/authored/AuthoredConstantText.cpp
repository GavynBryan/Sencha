#include <authored/AuthoredConstantText.h>

#include <core/text/ValueText.h>

#include <string>
#include <utility>
#include <vector>

const DataFieldSchema& UnwrappedField(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
}

std::optional<JsonValue> AuthoredConstantFromText(const DataFieldSchema& field, std::string_view text)
{
    const DataFieldKind kind = UnwrappedField(field).Kind;
    const auto wrapped = [](std::string_view member, JsonValue value) {
        return JsonValue(JsonValue::Object{ { std::string(member), std::move(value) } });
    };
    if (kind == DataFieldKind::Int || kind == DataFieldKind::Float)
    {
        const std::optional<double> number = ParseNumber(text);
        return number ? std::optional(wrapped("const", JsonValue(*number))) : std::nullopt;
    }
    if (kind == DataFieldKind::Bool)
    {
        const std::optional<bool> on = ParseToggle(text);
        return on ? std::optional(wrapped("const", JsonValue(*on))) : std::nullopt;
    }
    if (kind == DataFieldKind::Vector)
    {
        const std::optional<std::vector<double>> parts = ParseVector(text, UnwrappedField(field).VectorLength);
        if (!parts)
            return std::nullopt;
        JsonValue::Array components;
        for (const double part : *parts)
            components.emplace_back(part);
        return wrapped("const", JsonValue(std::move(components)));
    }
    if (kind == DataFieldKind::GameplayTag)
        return wrapped("tag", JsonValue(std::string(text)));
    if (kind == DataFieldKind::DataAssetRef)
        return wrapped("data", JsonValue(std::string(text)));
    if (kind == DataFieldKind::AssetRef)
        return wrapped("asset", JsonValue(std::string(text)));
    return wrapped("const", JsonValue(std::string(text)));
}
