#include "vocabulary/AuthoredConstantText.h"

#include "data/JsonObjectEdit.h"
#include "properties/SettingProperties.h"

#include <format>

namespace
{
bool Numeric(DataFieldKind kind)
{
    return kind == DataFieldKind::Int || kind == DataFieldKind::Float;
}
} // namespace

const DataFieldSchema& UnwrappedField(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
}

std::string AuthoredConstantText(const JsonValue& argument)
{
    const JsonValue* held = argument.IsObject() ? FindMember(argument, "const") : &argument;
    for (const std::string_view member : { "tag", "data", "asset" })
        if (const JsonValue* named = FindMember(argument, member); named != nullptr && named->IsString())
            return named->AsString();
    if (held == nullptr)
        return {};
    if (held->IsNumber())
        return NumberText(held->AsNumber());
    if (held->IsBool())
        return held->AsBool() ? "true" : "false";
    if (held->IsString())
        return held->AsString();
    if (held->IsArray())
    {
        std::string text;
        for (const JsonValue& component : held->AsArray())
            text += std::format("{}{}", text.empty() ? "" : ", ", component.IsNumber() ? component.AsNumber() : 0.0);
        return text;
    }
    return {};
}

std::optional<JsonValue> AuthoredConstantFromText(const DataFieldSchema& field, std::string_view text)
{
    const DataFieldKind kind = UnwrappedField(field).Kind;
    const auto wrapped = [](std::string_view member, JsonValue value) {
        return JsonValue(JsonValue::Object{ { std::string(member), std::move(value) } });
    };
    if (Numeric(kind))
    {
        const std::optional<double> number = ParseNumber(text, 0.0, -1.0);
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

JsonValue DefaultAuthoredConstant(const DataFieldSchema& field)
{
    const DataFieldKind kind = UnwrappedField(field).Kind;
    if (Numeric(kind))
        return *AuthoredConstantFromText(field, "0");
    if (kind == DataFieldKind::Bool)
        return *AuthoredConstantFromText(field, "false");
    if (kind == DataFieldKind::Vector)
        return *AuthoredConstantFromText(field, "0, 0, 0");
    return *AuthoredConstantFromText(field, "");
}
