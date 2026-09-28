#include <authored/AuthoredLiteral.h>

#include <algorithm>
#include <cmath>
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

    // JSON numbers are doubles. An identity, a count, or a frame budget that
    // arrived as 2^53 + 1 would silently become something else, so an integer
    // that cannot be represented exactly is refused rather than rounded.
    [[nodiscard]] bool ExactInteger(double value, std::int64_t& out)
    {
        if (!std::isfinite(value) || std::floor(value) != value)
            return false;
        constexpr double kMaxExact = 9007199254740992.0; // 2^53
        if (value > kMaxExact || value < -kMaxExact)
            return false;
        out = static_cast<std::int64_t>(value);
        return true;
    }

    [[nodiscard]] bool WithinRange(double value, const DataFieldSchema& field)
    {
        if (field.Numeric.Minimum && value < *field.Numeric.Minimum)
            return false;
        if (field.Numeric.Maximum && value > *field.Numeric.Maximum)
            return false;
        return true;
    }

    [[nodiscard]] std::string ElementPath(std::string_view parent, std::size_t index)
    {
        return std::format("{}[{}]", parent, index);
    }

    [[nodiscard]] std::string MemberPath(std::string_view parent, std::string_view key)
    {
        return parent.empty() ? std::string(key) : std::format("{}.{}", parent, key);
    }
}

bool CompileAuthoredDefault(const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors)
{
    // One definition of what an unsupplied argument means, here rather than
    // in each consumer: an explicit default if the field has one, absent if
    // the field tolerates absence, and a diagnostic otherwise.
    if (std::holds_alternative<bool>(field.Default))
        return CompileAuthoredLiteral(JsonValue(std::get<bool>(field.Default)), field, subject,
                                      path, out, errors);
    if (std::holds_alternative<std::int64_t>(field.Default))
        return CompileAuthoredLiteral(
            JsonValue(static_cast<double>(std::get<std::int64_t>(field.Default))), field,
            subject, path, out, errors);
    if (std::holds_alternative<double>(field.Default))
        return CompileAuthoredLiteral(JsonValue(std::get<double>(field.Default)), field, subject,
                                      path, out, errors);
    if (std::holds_alternative<std::string>(field.Default))
        return CompileAuthoredLiteral(JsonValue(std::get<std::string>(field.Default)), field,
                                      subject, path, out, errors);

    if (field.Required)
    {
        Fail(errors, subject, path, "a value is required and none is supplied");
        return false;
    }
    out = AuthoredValue{};
    return true;
}

bool CompileAuthoredLiteral(const JsonValue& value,
                            const DataFieldSchema& field,
                            std::string_view subject,
                            const std::string& path,
                            AuthoredValue& out,
                            std::vector<std::string>& errors)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        if (!value.IsBool())
        {
            Fail(errors, subject, path, "expected a boolean");
            return false;
        }
        out = AuthoredValue::Bool(value.AsBool());
        return true;

    case DataFieldKind::Int:
    {
        std::int64_t whole = 0;
        if (!value.IsNumber() || !ExactInteger(value.AsNumber(), whole))
        {
            Fail(errors, subject, path,
                 "expected an integer a 64-bit value can hold exactly");
            return false;
        }
        if (!WithinRange(value.AsNumber(), field))
        {
            Fail(errors, subject, path, "value is outside the declared range");
            return false;
        }
        out = AuthoredValue::Int(whole);
        return true;
    }

    case DataFieldKind::Float:
        if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
        {
            Fail(errors, subject, path, "expected a finite number");
            return false;
        }
        if (!WithinRange(value.AsNumber(), field))
        {
            Fail(errors, subject, path, "value is outside the declared range");
            return false;
        }
        out = AuthoredValue::Float(value.AsNumber());
        return true;

    case DataFieldKind::String:
        if (!value.IsString())
        {
            Fail(errors, subject, path, "expected a string");
            return false;
        }
        out = AuthoredValue::String(value.AsString());
        return true;

    case DataFieldKind::Enum:
    {
        if (!value.IsString())
        {
            Fail(errors, subject, path, "expected one of the declared choices");
            return false;
        }
        const bool known = std::ranges::any_of(
            field.EnumChoices,
            [&value](const DataEnumChoice& choice) { return choice.Value == value.AsString(); });
        if (!known)
        {
            Fail(errors, subject, path,
                 std::format("'{}' is not one of the declared choices", value.AsString()));
            return false;
        }
        out = AuthoredValue::Enum(value.AsString());
        return true;
    }

    case DataFieldKind::Vector:
    {
        if (!value.IsArray() || value.AsArray().size() != field.VectorLength)
        {
            Fail(errors, subject, path,
                 std::format("expected {} numbers", field.VectorLength));
            return false;
        }
        AuthoredVectorValue vector;
        vector.Length = static_cast<std::uint8_t>(field.VectorLength);
        for (std::size_t index = 0; index < field.VectorLength; ++index)
        {
            const JsonValue& element = value.AsArray()[index];
            if (!element.IsNumber() || !std::isfinite(element.AsNumber())
                || !WithinRange(element.AsNumber(), field))
            {
                Fail(errors, subject, ElementPath(path, index),
                     "expected a finite number within the declared range");
                return false;
            }
            vector.Components[index] = element.AsNumber();
        }
        out = AuthoredValue::Vector(vector);
        return true;
    }

    case DataFieldKind::Record:
    {
        if (!value.IsObject())
        {
            Fail(errors, subject, path, "expected an object");
            return false;
        }
        std::vector<AuthoredValue> members;
        members.reserve(field.Children.size());
        bool ok = true;
        for (const DataFieldSchema& child : field.Children)
        {
            const std::string childPath = MemberPath(path, child.Key);
            const JsonValue* member = value.Find(child.Key);
            AuthoredValue compiled;
            if (member == nullptr)
                ok = CompileAuthoredDefault(child, subject, childPath, compiled, errors) && ok;
            else
                ok = CompileAuthoredLiteral(*member, child, subject, childPath, compiled, errors)
                    && ok;
            members.push_back(std::move(compiled));
        }
        for (const auto& [key, unused] : value.AsObject())
        {
            (void)unused;
            if (FindChild(field, key) == nullptr)
            {
                Fail(errors, subject, MemberPath(path, key),
                     "the contract does not accept this member");
                ok = false;
            }
        }
        if (!ok)
            return false;
        out = AuthoredValue::Record(std::move(members));
        return true;
    }

    case DataFieldKind::Array:
    {
        if (!value.IsArray())
        {
            Fail(errors, subject, path, "expected an array");
            return false;
        }
        if (field.Children.size() != 1)
        {
            Fail(errors, subject, path, "the array field names no element shape");
            return false;
        }
        std::vector<AuthoredValue> elements;
        elements.reserve(value.AsArray().size());
        bool ok = true;
        for (std::size_t index = 0; index < value.AsArray().size(); ++index)
        {
            AuthoredValue element;
            ok = CompileAuthoredLiteral(value.AsArray()[index], field.Children.front(), subject,
                                ElementPath(path, index), element, errors)
                && ok;
            elements.push_back(std::move(element));
        }
        if (!ok)
            return false;
        out = AuthoredValue::Array(std::move(elements));
        return true;
    }

    case DataFieldKind::Optional:
        // An explicit null is a value: the author said "nothing here". It is
        // not the same as leaving the argument out, which takes the default.
        if (value.IsNull())
        {
            out = AuthoredValue{};
            return true;
        }
        if (field.Children.size() != 1)
        {
            Fail(errors, subject, path, "the optional field names no value shape");
            return false;
        }
        return CompileAuthoredLiteral(value, field.Children.front(), subject, path, out, errors);

    case DataFieldKind::AssetRef:
    case DataFieldKind::DataAssetRef:
        Fail(errors, subject, path,
             "an asset argument is written as {\"asset\": ...} or {\"data\": ...} so the "
             "dependency can be named before a World exists");
        return false;

    case DataFieldKind::GameplayTag:
        Fail(errors, subject, path,
             "a gameplay tag argument is written as {\"tag\": ...}");
        return false;

    case DataFieldKind::Entity:
        Fail(errors, subject, path,
             "an entity argument is written as {\"entity\": ...}");
        return false;
    }

    Fail(errors, subject, path, "the field declares a shape no literal can satisfy");
    return false;
}
