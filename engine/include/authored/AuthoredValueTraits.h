#pragma once

#include <authored/AuthoredValue.h>
#include <core/metadata/DataSchema.h>
#include <core/metadata/EnumSchema.h>
#include <math/Vec.h>

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// How a C++ type crosses the authored boundary: its schema, and decode/encode
// to AuthoredValue. A failed decode is a refusal, never a default. The numeric
// contract (int64 integers, double floats, enums by name) is described in
// docs/gameplay/authored-api.md.

template<typename T>
struct AuthoredValueTraits;

template<typename T>
concept IsAuthoredValueType = requires(const AuthoredValue& value, T& out, const T& in,
                                       DataFieldSchema& field) {
    AuthoredValueTraits<T>::Describe(field);
    { AuthoredValueTraits<T>::Decode(value, out) } -> std::same_as<bool>;
    { AuthoredValueTraits<T>::Encode(in) } -> std::same_as<AuthoredValue>;
};

// Integers an int64 holds exactly. Character types and bool are excluded.
template<typename T>
concept AuthoredInteger =
    std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char>
    && !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t>
    && !std::same_as<T, char32_t>
    && (std::is_signed_v<T> ? sizeof(T) <= sizeof(std::int64_t) : sizeof(T) <= sizeof(std::uint32_t));

template<typename T>
inline constexpr bool IsStdOptional = false;
template<typename T>
inline constexpr bool IsStdOptional<std::optional<T>> = true;

template<>
struct AuthoredValueTraits<bool>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::Bool; }
    static bool Decode(const AuthoredValue& value, bool& out) { return value.TryGetBool(out); }
    static AuthoredValue Encode(bool value) { return AuthoredValue::Bool(value); }
};

template<AuthoredInteger T>
struct AuthoredValueTraits<T>
{
    // A narrower type declares its range, so content cannot author a value it
    // would refuse.
    static void Describe(DataFieldSchema& field)
    {
        field.Kind = DataFieldKind::Int;
        if constexpr (sizeof(T) < sizeof(std::int64_t))
        {
            field.Numeric.Minimum = static_cast<double>(std::numeric_limits<T>::min());
            field.Numeric.Maximum = static_cast<double>(std::numeric_limits<T>::max());
        }
    }

    static bool Decode(const AuthoredValue& value, T& out)
    {
        std::int64_t whole = 0;
        if (!value.TryGetInt(whole) || !std::in_range<T>(whole))
            return false;
        out = static_cast<T>(whole);
        return true;
    }

    static AuthoredValue Encode(T value) { return AuthoredValue::Int(static_cast<std::int64_t>(value)); }
};

template<std::floating_point T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
struct AuthoredValueTraits<T>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::Float; }

    static bool Decode(const AuthoredValue& value, T& out)
    {
        double number = 0.0;
        if (!value.TryGetFloat(number) || !std::isfinite(number))
            return false;
        if constexpr (std::same_as<T, float>)
        {
            if (std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
                return false;
        }
        out = static_cast<T>(number);
        return true;
    }

    static AuthoredValue Encode(T value) { return AuthoredValue::Float(static_cast<double>(value)); }
};

template<>
struct AuthoredValueTraits<std::string>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::String; }

    static bool Decode(const AuthoredValue& value, std::string& out)
    {
        std::string_view text;
        if (!value.TryGetString(text))
            return false;
        out.assign(text);
        return true;
    }

    static AuthoredValue Encode(const std::string& value) { return AuthoredValue::String(value); }
};

template<typename E>
    requires(std::is_enum_v<E> && HasEnumSchema<E>)
struct AuthoredValueTraits<E>
{
    static void Describe(DataFieldSchema& field)
    {
        field.Kind = DataFieldKind::Enum;
        field.EnumChoices.clear();
        for (const auto& value : EnumSchema<E>::Values)
        {
            DataEnumChoice choice;
            choice.Value = std::string(value.Name);
            choice.DisplayName = std::string(value.Display);
            choice.Description = std::string(value.Tooltip);
            field.EnumChoices.push_back(std::move(choice));
        }
    }

    static bool Decode(const AuthoredValue& value, E& out)
    {
        std::string_view name;
        if (!value.TryGetEnum(name))
            return false;
        for (const auto& declared : EnumSchema<E>::Values)
        {
            if (declared.Name == name)
            {
                out = declared.Value;
                return true;
            }
        }
        return false;
    }

    // An unlisted enumerator encodes as None, which no enum field accepts.
    static AuthoredValue Encode(E value)
    {
        for (const auto& declared : EnumSchema<E>::Values)
        {
            if (declared.Value == value)
                return AuthoredValue::Enum(std::string(declared.Name));
        }
        return {};
    }
};

template<>
struct AuthoredValueTraits<EntityId>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::Entity; }
    static bool Decode(const AuthoredValue& value, EntityId& out) { return value.TryGetEntity(out); }
    static AuthoredValue Encode(EntityId value) { return AuthoredValue::Entity(value); }
};

template<>
struct AuthoredValueTraits<GameplayTagId>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::GameplayTag; }
    static bool Decode(const AuthoredValue& value, GameplayTagId& out) { return value.TryGetTag(out); }
    static AuthoredValue Encode(GameplayTagId value) { return AuthoredValue::Tag(value); }
};

template<>
struct AuthoredValueTraits<AssetRef>
{
    static void Describe(DataFieldSchema& field) { field.Kind = DataFieldKind::AssetRef; }

    static bool Decode(const AuthoredValue& value, AssetRef& out)
    {
        const AssetRef* reference = nullptr;
        if (!value.TryGetAsset(reference))
            return false;
        out = *reference;
        return true;
    }

    static AuthoredValue Encode(const AssetRef& value) { return AuthoredValue::Asset(value); }
};

// Floating-point only: an integer vector would narrow the double components.
template<int N, typename T>
    requires(N >= 2 && N <= 4 && (std::same_as<T, float> || std::same_as<T, double>))
struct AuthoredValueTraits<Vec<N, T>>
{
    static void Describe(DataFieldSchema& field)
    {
        field.Kind = DataFieldKind::Vector;
        field.VectorLength = static_cast<std::uint32_t>(N);
    }

    static bool Decode(const AuthoredValue& value, Vec<N, T>& out)
    {
        AuthoredVectorValue vector;
        if (!value.TryGetVector(vector) || vector.Length != N)
            return false;
        Vec<N, T> result;
        for (int index = 0; index < N; ++index)
        {
            T component{};
            if (!AuthoredValueTraits<T>::Decode(AuthoredValue::Float(vector.Components[index]),
                                                component))
            {
                return false;
            }
            result[index] = component;
        }
        out = result;
        return true;
    }

    static AuthoredValue Encode(const Vec<N, T>& value)
    {
        AuthoredVectorValue vector;
        vector.Length = static_cast<std::uint8_t>(N);
        for (int index = 0; index < N; ++index)
            vector.Components[index] = static_cast<double>(value[index]);
        return AuthoredValue::Vector(vector);
    }
};

// The only way to declare an absent value. One level only.
template<IsAuthoredValueType T>
    requires(!IsStdOptional<T>)
struct AuthoredValueTraits<std::optional<T>>
{
    static void Describe(DataFieldSchema& field)
    {
        field.Kind = DataFieldKind::Optional;
        field.Required = false;
        DataFieldSchema inner;
        AuthoredValueTraits<T>::Describe(inner);
        field.Children.clear();
        field.Children.push_back(std::move(inner));
    }

    static bool Decode(const AuthoredValue& value, std::optional<T>& out)
    {
        if (value.IsNone())
        {
            out.reset();
            return true;
        }
        T inner{};
        if (!AuthoredValueTraits<T>::Decode(value, inner))
            return false;
        out = std::move(inner);
        return true;
    }

    static AuthoredValue Encode(const std::optional<T>& value)
    {
        return value ? AuthoredValueTraits<T>::Encode(*value) : AuthoredValue{};
    }
};

// The field a present value is checked against: the optional's element, or
// the field itself. Ranges and target components go here.
[[nodiscard]] inline DataFieldSchema& AuthoredValueField(DataFieldSchema& field)
{
    if (field.Kind == DataFieldKind::Optional && field.Children.size() == 1)
        return field.Children.front();
    return field;
}

// Ordered, and inside what T holds. Checked by generated static_asserts.
template<typename T>
consteval bool AuthoredRangeFits(long double minimum, long double maximum)
{
    if constexpr (IsStdOptional<T>)
        return AuthoredRangeFits<typename T::value_type>(minimum, maximum);
    else if constexpr (AuthoredInteger<T>)
        return minimum <= maximum
            && minimum >= static_cast<long double>(std::numeric_limits<T>::min())
            && maximum <= static_cast<long double>(std::numeric_limits<T>::max());
    else if constexpr (std::same_as<T, float> || std::same_as<T, double>)
        return minimum <= maximum
            && minimum >= -static_cast<long double>(std::numeric_limits<T>::max())
            && maximum <= static_cast<long double>(std::numeric_limits<T>::max());
    else
        return false;
}

// C++ values a schema default can hold: DataDefaultValue spells only a bool,
// integer, double or string, so entity, tag, asset and vector defaults are
// refused rather than dropped.

template<typename T>
struct AuthoredSchemaDefault;

template<typename T>
concept CanRepresentSchemaDefault = requires(const T& value) {
    { AuthoredSchemaDefault<T>::ToDefault(value) } -> std::same_as<DataDefaultValue>;
};

template<>
struct AuthoredSchemaDefault<bool>
{
    static DataDefaultValue ToDefault(bool value) { return value; }
};

template<AuthoredInteger T>
struct AuthoredSchemaDefault<T>
{
    static DataDefaultValue ToDefault(T value) { return static_cast<std::int64_t>(value); }
};

template<std::floating_point T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
struct AuthoredSchemaDefault<T>
{
    static DataDefaultValue ToDefault(T value) { return static_cast<double>(value); }
};

template<>
struct AuthoredSchemaDefault<std::string>
{
    static DataDefaultValue ToDefault(const std::string& value) { return value; }
};

template<typename E>
    requires(std::is_enum_v<E> && HasEnumSchema<E>)
struct AuthoredSchemaDefault<E>
{
    // An unlisted enumerator yields "", which validation rejects.
    static DataDefaultValue ToDefault(E value)
    {
        for (const auto& declared : EnumSchema<E>::Values)
        {
            if (declared.Value == value)
                return std::string(declared.Name);
        }
        return std::string{};
    }
};

// An empty optional is no default.
template<CanRepresentSchemaDefault T>
struct AuthoredSchemaDefault<std::optional<T>>
{
    static DataDefaultValue ToDefault(const std::optional<T>& value)
    {
        return value ? AuthoredSchemaDefault<T>::ToDefault(*value) : DataDefaultValue{};
    }
};
