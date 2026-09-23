#include <authored/AuthoredSchema.h>

#include <algorithm>
#include <format>

namespace
{
    [[nodiscard]] bool IsNameStart(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    [[nodiscard]] bool IsNameBody(char c)
    {
        return IsNameStart(c) || (c >= '0' && c <= '9');
    }

    [[nodiscard]] bool DefaultMatchesKind(const DataFieldSchema& field)
    {
        if (std::holds_alternative<std::monostate>(field.Default))
            return true;

        switch (field.Kind)
        {
        case DataFieldKind::Bool:
            return std::holds_alternative<bool>(field.Default);
        case DataFieldKind::Int:
            return std::holds_alternative<int64_t>(field.Default);
        case DataFieldKind::Float:
            return std::holds_alternative<double>(field.Default)
                || std::holds_alternative<int64_t>(field.Default);
        case DataFieldKind::Enum:
        {
            const std::string* choice = std::get_if<std::string>(&field.Default);
            return choice
                && std::ranges::any_of(field.EnumChoices, [&](const DataEnumChoice& declared) {
                       return declared.Value == *choice;
                   });
        }
        case DataFieldKind::String:
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
        case DataFieldKind::GameplayTag:
        case DataFieldKind::Entity:
            return std::holds_alternative<std::string>(field.Default);
        case DataFieldKind::Optional:
            // Checked as a default for the wrapped element.
            if (field.Children.size() != 1)
                return false;
            {
                DataFieldSchema inner = field.Children.front();
                inner.Default = field.Default;
                return DefaultMatchesKind(inner);
            }
        case DataFieldKind::Vector:
        case DataFieldKind::Record:
        case DataFieldKind::Array:
            // DataDefaultValue cannot spell these.
            return false;
        }
        return false;
    }
}

bool IsValidAuthoredName(std::string_view name)
{
    if (name.empty())
        return false;

    std::size_t segmentLength = 0;
    for (const char c : name)
    {
        if (c == '.')
        {
            if (segmentLength == 0)
                return false;
            segmentLength = 0;
            continue;
        }
        const bool ok = segmentLength == 0 ? IsNameStart(c) : IsNameBody(c);
        if (!ok)
            return false;
        ++segmentLength;
    }
    return segmentLength != 0;
}

void ValidateAuthoredField(const DataFieldSchema& field,
                           const std::string& path,
                           std::vector<std::string>& errors)
{
    if (!DefaultMatchesKind(field))
        errors.push_back(std::format("{}: default value does not match the field kind", path));

    if (field.Numeric.Minimum && field.Numeric.Maximum
        && *field.Numeric.Minimum > *field.Numeric.Maximum)
    {
        errors.push_back(std::format("{}: minimum is above maximum", path));
    }

    if (!field.Reference.ComponentIdentity.empty() && field.Kind != DataFieldKind::Entity)
    {
        errors.push_back(
            std::format("{}: only an entity can be expected to carry a component", path));
    }

    switch (field.Kind)
    {
    case DataFieldKind::Record:
    {
        std::vector<std::string_view> seen;
        seen.reserve(field.Children.size());
        for (const DataFieldSchema& child : field.Children)
        {
            if (child.Key.empty())
            {
                errors.push_back(std::format("{}: a record member needs a key", path));
                continue;
            }
            if (std::ranges::find(seen, std::string_view(child.Key)) != seen.end())
            {
                errors.push_back(
                    std::format("{}: duplicate member key '{}'", path, child.Key));
                continue;
            }
            seen.push_back(child.Key);
            ValidateAuthoredField(child, std::format("{}.{}", path, child.Key), errors);
        }
        break;
    }
    case DataFieldKind::Array:
    case DataFieldKind::Optional:
        if (field.Children.size() != 1)
        {
            errors.push_back(std::format(
                "{}: an array or optional describes exactly one element", path));
            break;
        }
        ValidateAuthoredField(field.Children.front(), std::format("{}[]", path), errors);
        break;
    case DataFieldKind::Enum:
    {
        if (field.EnumChoices.empty())
        {
            errors.push_back(std::format("{}: an enum needs at least one choice", path));
            break;
        }
        std::vector<std::string_view> seen;
        seen.reserve(field.EnumChoices.size());
        for (const DataEnumChoice& choice : field.EnumChoices)
        {
            if (choice.Value.empty())
            {
                errors.push_back(std::format("{}: an enum choice needs a value", path));
                continue;
            }
            if (std::ranges::find(seen, std::string_view(choice.Value)) != seen.end())
                errors.push_back(
                    std::format("{}: duplicate enum choice '{}'", path, choice.Value));
            else
                seen.push_back(choice.Value);
        }
        break;
    }
    case DataFieldKind::Vector:
        if (field.VectorLength < 2 || field.VectorLength > 4)
            errors.push_back(std::format("{}: a vector is 2, 3 or 4 wide", path));
        break;
    case DataFieldKind::Bool:
    case DataFieldKind::Int:
    case DataFieldKind::Float:
    case DataFieldKind::String:
    case DataFieldKind::AssetRef:
    case DataFieldKind::DataAssetRef:
    case DataFieldKind::GameplayTag:
    case DataFieldKind::Entity:
        break;
    }
}

bool AuthoredContractsMatch(const DataFieldSchema& left, const DataFieldSchema& right)
{
    if (left.Key != right.Key || left.Kind != right.Kind || left.Required != right.Required
        || left.VectorLength != right.VectorLength || left.Default != right.Default)
    {
        return false;
    }
    // Step is presentation.
    if (left.Numeric.Minimum != right.Numeric.Minimum
        || left.Numeric.Maximum != right.Numeric.Maximum)
    {
        return false;
    }
    if (left.Reference.AssetTypeFilter != right.Reference.AssetTypeFilter
        || left.Reference.DataSubtype != right.Reference.DataSubtype)
    {
        return false;
    }
    if (left.EnumChoices.size() != right.EnumChoices.size())
        return false;
    for (std::size_t index = 0; index < left.EnumChoices.size(); ++index)
    {
        if (left.EnumChoices[index].Value != right.EnumChoices[index].Value)
            return false;
    }
    if (left.Children.size() != right.Children.size())
        return false;
    for (std::size_t index = 0; index < left.Children.size(); ++index)
    {
        if (!AuthoredContractsMatch(left.Children[index], right.Children[index]))
            return false;
    }
    return true;
}
