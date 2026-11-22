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

JsonValue DefaultAuthoredConstant(const DataFieldSchema& field)
{
    const DataFieldKind kind = UnwrappedField(field).Kind;
    if (Numeric(kind))
        return *AuthoredConstantFromText(field, "0");
    if (kind == DataFieldKind::Bool)
        return *AuthoredConstantFromText(field, "false");
    if (kind == DataFieldKind::Vector)
        return *AuthoredConstantFromText(field, UnwrappedField(field).Editor.Widget == "direction" ? "0, 0, 1" : "0, 0, 0");
    return *AuthoredConstantFromText(field, "");
}
