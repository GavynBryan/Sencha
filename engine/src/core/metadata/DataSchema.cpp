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

    bool ValidateNumber(double value,
                        const DataFieldSchema& field,
                        std::string_view path,
                        std::vector<DataValidationError>& errors)
    {
        bool valid = true;
        if (!std::isfinite(value))
        {
            AddError(errors, path, "value must be finite");
            return false;
        }

        if (field.Numeric.Minimum && value < *field.Numeric.Minimum)
        {
            AddError(errors, path,
                     std::format("value {} is below minimum {}", value,
                                 *field.Numeric.Minimum));
            valid = false;
        }
        if (field.Numeric.Maximum && value > *field.Numeric.Maximum)
        {
            AddError(errors, path,
                     std::format("value {} is above maximum {}", value,
                                 *field.Numeric.Maximum));
            valid = false;
        }
        return valid;
    }

    bool ValidateField(const JsonValue& value,
                       const DataFieldSchema& field,
                       std::string_view path,
                       bool allowUnknownFields,
                       std::vector<DataValidationError>& errors)
    {
        switch (field.Kind)
        {
        case DataFieldKind::Bool:
            if (!value.IsBool())
            {
                AddError(errors, path, "expected boolean");
                return false;
            }
            return true;

        case DataFieldKind::Int:
            if (!value.IsNumber())
            {
                AddError(errors, path, "expected integer");
                return false;
            }
            if (std::floor(value.AsNumber()) != value.AsNumber())
            {
                AddError(errors, path, "expected integer without a fractional part");
                return false;
            }
            return ValidateNumber(value.AsNumber(), field, path, errors);

        case DataFieldKind::Float:
            if (!value.IsNumber())
            {
                AddError(errors, path, "expected number");
                return false;
            }
            return ValidateNumber(value.AsNumber(), field, path, errors);

        case DataFieldKind::String:
            if (!value.IsString())
            {
                AddError(errors, path, "expected string");
                return false;
            }
            return true;

        case DataFieldKind::Enum:
        {
            if (!value.IsString())
            {
                AddError(errors, path, "expected enum string");
                return false;
            }

            const auto it = std::find_if(field.EnumChoices.begin(), field.EnumChoices.end(),
                [&value](const DataEnumChoice& choice)
                {
                    return choice.Value == value.AsString();
                });
            if (it == field.EnumChoices.end())
            {
                AddError(errors, path,
                         std::format("unknown enum value '{}'", value.AsString()));
                return false;
            }
            return true;
        }

        case DataFieldKind::Vector:
        {
            if (!value.IsArray())
            {
                AddError(errors, path, "expected numeric array");
                return false;
            }
            if (value.Size() != field.VectorLength)
            {
                AddError(errors, path,
                         std::format("expected {} vector elements", field.VectorLength));
                return false;
            }

            bool valid = true;
            for (size_t index = 0; index < value.AsArray().size(); ++index)
            {
                const JsonValue& element = value.AsArray()[index];
                const std::string elementPath =
                    std::format("{}[{}]", path, index);
                if (!element.IsNumber())
                {
                    AddError(errors, elementPath, "expected number");
                    valid = false;
                    continue;
                }
                valid = ValidateNumber(element.AsNumber(), field, elementPath, errors) && valid;
            }
            return valid;
        }

        case DataFieldKind::Record:
        {
            if (!value.IsObject())
            {
                AddError(errors, path, "expected object");
                return false;
            }

            bool valid = true;
            for (const DataFieldSchema& child : field.Children)
            {
                const JsonValue* childValue = value.Find(child.Key);
                const std::string childPath = ChildPath(path, child.Key);
                if (childValue == nullptr)
                {
                    if (child.Required)
                    {
                        AddError(errors, childPath, "required field is missing");
                        valid = false;
                    }
                    continue;
                }
                valid = ValidateField(*childValue, child, childPath,
                                      allowUnknownFields, errors) && valid;
            }

            if (!allowUnknownFields)
            {
                for (const auto& [key, unused] : value.AsObject())
                {
                    (void)unused;
                    const auto it = std::find_if(field.Children.begin(), field.Children.end(),
                        [&key](const DataFieldSchema& child)
                        {
                            return child.Key == key;
                        });
                    if (it == field.Children.end())
                    {
                        AddError(errors, ChildPath(path, key), "unknown field");
                        valid = false;
                    }
                }
            }
            return valid;
        }

        case DataFieldKind::Array:
        {
            if (!value.IsArray())
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
            for (size_t index = 0; index < value.AsArray().size(); ++index)
            {
                const std::string elementPath = std::format("{}[{}]", path, index);
                valid = ValidateField(value.AsArray()[index], field.Children.front(),
                                      elementPath, allowUnknownFields, errors) && valid;
            }
            return valid;
        }

        case DataFieldKind::Optional:
            if (value.IsNull())
                return true;
            if (field.Children.size() != 1)
            {
                AddError(errors, path, "optional schema must define exactly one value schema");
                return false;
            }
            return ValidateField(value, field.Children.front(), path,
                                 allowUnknownFields, errors);

        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
            if (!value.IsString())
            {
                AddError(errors, path, "expected asset path string");
                return false;
            }
            if (!value.AsString().starts_with("asset://"))
            {
                AddError(errors, path, "asset path must start with 'asset://'");
                return false;
            }
            return true;

        case DataFieldKind::GameplayTag:
            if (!value.IsString())
            {
                AddError(errors, path, "expected gameplay tag string");
                return false;
            }
            if (value.AsString().empty())
            {
                AddError(errors, path, "gameplay tag cannot be empty");
                return false;
            }
            return true;

        case DataFieldKind::Entity:
            if (!value.IsString())
            {
                AddError(errors, path, "expected persistent entity id string");
                return false;
            }
            // Strict, like PersistentEntityIdFromString: a malformed identity
            // that parsed leniently would resolve to nothing at load and read
            // as a missing entity rather than as the typo it is.
            if (!PersistentEntityIdFromString(value.AsString()).has_value())
            {
                AddError(errors, path,
                         "expected 16 lowercase hex digits naming a persistent entity");
                return false;
            }
            return true;
        }

        AddError(errors, path, "unsupported schema field kind");
        return false;
    }
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
    const bool valid = ValidateField(value, schema.Root, "$", schema.AllowUnknownFields,
                                     errors);
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
