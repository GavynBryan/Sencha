#include <core/assets/AssetRefCodec.h>
#include <core/metadata/DataSchema.h>

#include <core/identity/Id.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    void AddError(std::vector<DataValidationError>& errors,
                  std::string_view path,
                  std::string message)
    {
        errors.push_back(DataValidationError{
            .Path = std::string(path),
            .Message = std::move(message),
        });
    }

    std::string ChildPath(std::string_view parent, std::string_view key)
    {
        if (parent.empty() || parent == "$")
            return std::string("$.") + std::string(key);
        return std::string(parent) + "." + std::string(key);
    }

    // JSON numbers are doubles. An identity, a count, or a frame budget that
    // arrived as 2^53 + 1 would silently become something else, so an integer
    // a double cannot hold exactly is refused rather than rounded.
    bool IsExactInteger(double value)
    {
        constexpr double kMaxExact = 9007199254740992.0; // 2^53
        return std::isfinite(value) && std::floor(value) == value
            && value <= kMaxExact && value >= -kMaxExact;
    }

    bool CheckNumber(double value,
                     const DataFieldSchema& field,
                     std::string_view path,
                     std::vector<DataValidationError>& errors)
    {
        if (!std::isfinite(value))
        {
            AddError(errors, path, "value must be finite");
            return false;
        }
        bool valid = true;
        if (field.Numeric.Minimum && value < *field.Numeric.Minimum)
        {
            AddError(errors, path,
                     std::format("value {} is below minimum {}", value, *field.Numeric.Minimum));
            valid = false;
        }
        if (field.Numeric.Maximum && value > *field.Numeric.Maximum)
        {
            AddError(errors, path,
                     std::format("value {} is above maximum {}", value, *field.Numeric.Maximum));
            valid = false;
        }
        return valid;
    }

    // An array of records whose elements carry a StableKey: no two may share one.
    bool KeysUnique(const JsonValue& array,
                    const DataFieldSchema& element,
                    std::string_view path,
                    std::vector<DataValidationError>& errors)
    {
        if (element.Kind != DataFieldKind::Record)
            return true;
        bool valid = true;
        for (const DataFieldSchema& member : element.Children)
        {
            if (member.Kind != DataFieldKind::StableKey)
                continue;
            const JsonValue::Array& values = array.AsArray();
            for (size_t index = 0; index < values.size(); ++index)
            {
                const JsonValue* key = values[index].IsObject() ? values[index].Find(member.Key) : nullptr;
                if (key == nullptr || !key->IsString())
                    continue;
                for (size_t earlier = 0; earlier < index; ++earlier)
                {
                    const JsonValue* other = values[earlier].IsObject() ? values[earlier].Find(member.Key) : nullptr;
                    if (other != nullptr && other->IsString() && other->AsString() == key->AsString())
                    {
                        AddError(errors, ChildPath(std::format("{}[{}]", path, index), member.Key),
                                 std::format("key '{}' is already used by element {}", key->AsString(), earlier));
                        valid = false;
                        break;
                    }
                }
            }
        }
        return valid;
    }

    std::string CheckReferenceText(const JsonValue& value, const DataFieldSchema& field)
    {
        switch (field.Kind)
        {
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
            return AssetRefFromJson(value).has_value()
                ? std::string{}
                : "expected an asset path or an {id, path} reference";
        case DataFieldKind::GameplayTag:
            if (!value.IsString())
                return "expected gameplay tag string";
            return value.AsString().empty() ? "gameplay tag cannot be empty" : std::string{};
        case DataFieldKind::Entity:
            if (!value.IsString())
                return "expected persistent entity id string";
            // Strict, like PersistentEntityIdFromString: a malformed identity
            // that parsed leniently would resolve to nothing at load and read
            // as a missing entity rather than as the typo it is.
            return PersistentEntityIdFromString(value.AsString()).has_value()
                ? std::string{}
                : "expected 16 lowercase hex digits naming a persistent entity";
        default:
            return "not a reference field";
        }
    }

    JsonValue DefaultAsJson(const DataDefaultValue& value)
    {
        if (const bool* flag = std::get_if<bool>(&value))
            return JsonValue(*flag);
        if (const int64_t* whole = std::get_if<int64_t>(&value))
            return JsonValue(static_cast<double>(*whole));
        if (const double* number = std::get_if<double>(&value))
            return JsonValue(*number);
        if (const std::string* text = std::get_if<std::string>(&value))
            return JsonValue(*text);
        return JsonValue{};
    }
}

bool NormalizeDataValue(const JsonValue* value,
                        const DataFieldSchema& field,
                        std::string_view path,
                        const DataValueRules& rules,
                        JsonValue* out,
                        std::vector<DataValidationError>& errors)
{
    if (value == nullptr)
    {
        if (rules.FillDefaults && !std::holds_alternative<std::monostate>(field.Default))
        {
            const JsonValue fallback = DefaultAsJson(field.Default);
            return NormalizeDataValue(&fallback, field, path, rules, out, errors);
        }
        if (field.Required)
        {
            AddError(errors, path, "required field is missing");
            return false;
        }
        if (out != nullptr)
            *out = JsonValue{};
        return true;
    }

    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        if (!value->IsBool())
        {
            AddError(errors, path, "expected boolean");
            return false;
        }
        break;

    case DataFieldKind::Int:
        if (!value->IsNumber() || !IsExactInteger(value->AsNumber()))
        {
            AddError(errors, path, "expected an integer a 64-bit value can hold exactly");
            return false;
        }
        if (!CheckNumber(value->AsNumber(), field, path, errors))
            return false;
        break;

    case DataFieldKind::Float:
        if (!value->IsNumber())
        {
            AddError(errors, path, "expected number");
            return false;
        }
        if (!CheckNumber(value->AsNumber(), field, path, errors))
            return false;
        break;

    case DataFieldKind::String:
        if (!value->IsString())
        {
            AddError(errors, path, "expected string");
            return false;
        }
        break;

    case DataFieldKind::Enum:
    {
        if (!value->IsString())
        {
            AddError(errors, path, "expected enum string");
            return false;
        }
        const bool known = std::any_of(field.EnumChoices.begin(), field.EnumChoices.end(),
            [value](const DataEnumChoice& choice) { return choice.Value == value->AsString(); });
        if (!known)
        {
            AddError(errors, path, std::format("unknown enum value '{}'", value->AsString()));
            return false;
        }
        break;
    }

    case DataFieldKind::Vector:
    {
        if (!value->IsArray())
        {
            AddError(errors, path, "expected numeric array");
            return false;
        }
        if (value->Size() != field.VectorLength)
        {
            AddError(errors, path, std::format("expected {} vector elements", field.VectorLength));
            return false;
        }
        bool valid = true;
        for (size_t index = 0; index < value->AsArray().size(); ++index)
        {
            const JsonValue& element = value->AsArray()[index];
            const std::string elementPath = std::format("{}[{}]", path, index);
            if (!element.IsNumber())
            {
                AddError(errors, elementPath, "expected number");
                valid = false;
                continue;
            }
            valid = CheckNumber(element.AsNumber(), field, elementPath, errors) && valid;
        }
        if (!valid)
            return false;
        break;
    }

    case DataFieldKind::Record:
    {
        if (!value->IsObject())
        {
            AddError(errors, path, "expected object");
            return false;
        }
        bool valid = true;
        JsonValue::Object members;
        for (const DataFieldSchema& child : field.Children)
        {
            JsonValue member;
            const JsonValue* authored = value->Find(child.Key);
            // Left out and nothing to fill it with: it stays out, so what comes
            // back satisfies the schema it was checked against.
            const bool fills = rules.FillDefaults && !std::holds_alternative<std::monostate>(child.Default);
            if (authored == nullptr && !child.Required && !fills)
                continue;
            valid = NormalizeDataValue(authored, child, ChildPath(path, child.Key), rules,
                                       out != nullptr ? &member : nullptr, errors)
                && valid;
            if (out != nullptr)
                members.emplace_back(child.Key, std::move(member));
        }
        if (!rules.AllowUnknownFields)
        {
            for (const auto& [key, unused] : value->AsObject())
            {
                (void)unused;
                if (FindChild(field, key) == nullptr)
                {
                    AddError(errors, ChildPath(path, key), "unknown field");
                    valid = false;
                }
            }
        }
        if (!valid)
            return false;
        if (out != nullptr)
            *out = JsonValue(std::move(members));
        return true;
    }

    case DataFieldKind::Array:
    {
        if (!value->IsArray())
        {
            AddError(errors, path, "expected array");
            return false;
        }
        if (field.Children.size() != 1)
        {
            AddError(errors, path, "array schema must define exactly one element schema");
            return false;
        }
        bool valid = true;
        JsonValue::Array elements;
        for (size_t index = 0; index < value->AsArray().size(); ++index)
        {
            JsonValue element;
            valid = NormalizeDataValue(&value->AsArray()[index], field.Children.front(),
                                       std::format("{}[{}]", path, index), rules,
                                       out != nullptr ? &element : nullptr, errors)
                && valid;
            if (out != nullptr)
                elements.push_back(std::move(element));
        }
        valid = KeysUnique(*value, field.Children.front(), path, errors) && valid;
        if (!valid)
            return false;
        if (out != nullptr)
            *out = JsonValue(std::move(elements));
        return true;
    }

    case DataFieldKind::Optional:
        // An explicit null is a value: the author said "nothing here". It is
        // not the same as leaving the member out, which takes the default.
        if (value->IsNull())
            break;
        if (field.Children.size() != 1)
        {
            AddError(errors, path, "optional schema must define exactly one value schema");
            return false;
        }
        return NormalizeDataValue(value, field.Children.front(), path, rules, out, errors);

    case DataFieldKind::AssetRef:
    case DataFieldKind::DataAssetRef:
    case DataFieldKind::GameplayTag:
    case DataFieldKind::Entity:
    {
        const std::string refusal = rules.CheckReference
            ? rules.CheckReference(*value, field)
            : CheckReferenceText(*value, field);
        if (!refusal.empty())
        {
            AddError(errors, path, refusal);
            return false;
        }
        break;
    }

    case DataFieldKind::StableKey:
        if (!value->IsString() || !IsValidStableKey(value->AsString()))
        {
            AddError(errors, path, "expected a key of up to 64 letters, digits, '_' or '-'");
            return false;
        }
        break;

    default:
        AddError(errors, path, "unsupported schema field kind");
        return false;
    }

    if (out != nullptr)
        *out = *value;
    return true;

}

DataFieldSchema MakeDataField(DataFieldKind kind, std::string key, std::string displayName,
                              std::string summary)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.DisplayName = std::move(displayName);
    field.Summary = std::move(summary);
    field.Kind = kind;
    return field;
}

const DataFieldSchema* FindChild(const DataFieldSchema& parent, std::string_view key)
{
    for (const DataFieldSchema& child : parent.Children)
    {
        if (child.Key == key)
            return &child;
    }
    return nullptr;
}

bool ValidateDataAgainstSchema(const JsonValue& value,
                               const DataSchema& schema,
                               std::vector<DataValidationError>& errors)
{
    const size_t errorCount = errors.size();
    DataValueRules rules;
    rules.AllowUnknownFields = schema.AllowUnknownFields;
    const bool valid = NormalizeDataValue(&value, schema.Root, "$", rules, nullptr, errors);
    return valid && errors.size() == errorCount;
}

std::string FormatDataValidationErrors(std::span<const DataValidationError> errors)
{
    std::string result;
    const size_t count = std::min<size_t>(errors.size(), 8);
    for (size_t index = 0; index < count; ++index)
    {
        if (!result.empty())
            result += "; ";
        result += std::format("{}: {}", errors[index].Path, errors[index].Message);
    }
    if (errors.size() > count)
        result += std::format("; {} more errors", errors.size() - count);
    return result;
}

bool DataSchemaRegistry::Register(DataSchema schema)
{
    if (schema.TypeName.empty() || Find(schema.TypeName) != nullptr)
        return false;

    Schemas.push_back(std::move(schema));
    return true;
}

bool DataSchemaRegistry::Unregister(std::string_view typeName)
{
    auto it = std::find_if(Schemas.begin(), Schemas.end(),
        [typeName](const DataSchema& schema)
        {
            return schema.TypeName == typeName;
        });
    if (it == Schemas.end())
        return false;

    Schemas.erase(it);
    return true;
}

const DataSchema* DataSchemaRegistry::Find(std::string_view typeName) const
{
    const auto it = std::find_if(Schemas.begin(), Schemas.end(),
        [typeName](const DataSchema& schema)
        {
            return schema.TypeName == typeName;
        });
    return it == Schemas.end() ? nullptr : &*it;
}

std::span<const DataSchema> DataSchemaRegistry::Entries() const
{
    return Schemas;
}

bool IsValidStableKey(std::string_view text)
{
    if (text.empty() || text.size() > 64)
        return false;
    return std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
