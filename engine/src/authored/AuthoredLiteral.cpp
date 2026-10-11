#include <authored/AuthoredLiteral.h>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    void Fail(std::vector<std::string>& errors,
              std::string_view subject,
              std::string_view path,
              std::string message)
    {
        if (path.empty())
            errors.push_back(std::format("{}: {}", subject, message));
        else
            errors.push_back(std::format("{} argument '{}': {}", subject, path, message));
    }

    // Reference fields are spelled as reference sources ({"asset": ...},
    // {"tag": ...}, {"entity": ...}), never as literals, so the dependency is
    // named before a World exists.
    std::string RefuseReferenceLiteral(const JsonValue&, const DataFieldSchema& field)
    {
        switch (field.Kind)
        {
        case DataFieldKind::AssetRef:
        case DataFieldKind::DataAssetRef:
            return "an asset argument is written as {\"asset\": ...} or {\"data\": ...} so the "
                   "dependency can be named before a World exists";
        case DataFieldKind::GameplayTag:
            return "a gameplay tag argument is written as {\"tag\": ...}";
        default:
            return "an entity argument is written as {\"entity\": ...}";
        }
    }

    const DataValueRules& LiteralRules()
    {
        static const DataValueRules rules = [] {
            DataValueRules made;
            made.FillDefaults = true;
            made.CheckReference = RefuseReferenceLiteral;
            return made;
        }();
        return rules;
    }

    // Normalized JSON to the positional value a dispatcher reads. The value has
    // already been checked against `field`, so this only changes shape.
    AuthoredValue ToAuthoredValue(const JsonValue& value, const DataFieldSchema& field)
    {
        if (value.IsNull())
            return AuthoredValue{};
        switch (field.Kind)
        {
        case DataFieldKind::Bool:
            return AuthoredValue::Bool(value.AsBool());
        case DataFieldKind::Int:
            return AuthoredValue::Int(static_cast<std::int64_t>(value.AsNumber()));
        case DataFieldKind::Float:
            return AuthoredValue::Float(value.AsNumber());
        case DataFieldKind::String:
            return AuthoredValue::String(value.AsString());
        case DataFieldKind::Enum:
            return AuthoredValue::Enum(value.AsString());
        case DataFieldKind::Vector:
        {
            AuthoredVectorValue vector;
            vector.Length = static_cast<std::uint8_t>(field.VectorLength);
            for (std::size_t index = 0; index < field.VectorLength; ++index)
                vector.Components[index] = value.AsArray()[index].AsNumber();
            return AuthoredValue::Vector(vector);
        }
        case DataFieldKind::Record:
        {
            std::vector<AuthoredValue> members;
            members.reserve(field.Children.size());
            for (const DataFieldSchema& child : field.Children)
            {
                const JsonValue* member = value.Find(child.Key);
                members.push_back(member != nullptr ? ToAuthoredValue(*member, child) : AuthoredValue{});
            }
            return AuthoredValue::Record(std::move(members));
        }
        case DataFieldKind::Array:
        {
            std::vector<AuthoredValue> elements;
            elements.reserve(value.AsArray().size());
            for (const JsonValue& element : value.AsArray())
                elements.push_back(ToAuthoredValue(element, field.Children.front()));
            return AuthoredValue::Array(std::move(elements));
        }
        case DataFieldKind::Optional:
            return ToAuthoredValue(value, field.Children.front());
        default:
            return AuthoredValue{};
        }
    }

    // A stable key is minted by the document that holds it, never written as an argument.
    bool HoldsStableKey(const DataFieldSchema& field)
    {
        return field.Kind == DataFieldKind::StableKey || std::ranges::any_of(field.Children, HoldsStableKey);
    }

    // A literal, or an absent value (null), against the field: the one check
    // NormalizeDataValue makes for every authored value.
    bool Compile(const JsonValue* value,
                 const DataFieldSchema& field,
                 std::string_view subject,
                 const std::string& path,
                 AuthoredValue& out,
                 std::vector<std::string>& errors)
    {
        if (HoldsStableKey(field))
        {
            Fail(errors, subject, path, "the field declares a shape no literal can satisfy");
            return false;
        }
        JsonValue normalized;
        std::vector<DataValidationError> problems;
        if (!NormalizeDataValue(value, field, path, LiteralRules(), &normalized, problems))
        {
            for (const DataValidationError& problem : problems)
                Fail(errors, subject, problem.Path, problem.Message);
            return false;
        }
        out = ToAuthoredValue(normalized, field);
        return true;
    }
}

bool CompileAuthoredDefault(const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors)
{
    return Compile(nullptr, field, subject, path, out, errors);
}

bool CompileAuthoredLiteral(const JsonValue& value,
                            const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors)
{
    return Compile(&value, field, subject, path, out, errors);
}
