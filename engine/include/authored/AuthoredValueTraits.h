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

//=============================================================================
// AuthoredValueTraits
//
// How an ordinary C++ type crosses the authored boundary: what DataFieldSchema
// describes it, how an AuthoredValue becomes one, and how one becomes an
// AuthoredValue. Generated adapters are written in terms of this and nothing
// else, so the rule for a type lives here once rather than in the generator,
// the dispatcher and every hand-written operation.
//
// The numeric contract is deliberately narrow. An authored integer is a signed
// 64-bit value, so a type is supported only when every value it can hold fits
// exactly -- signed types up to 64 bits, unsigned types up to 32 -- and decoding
// into a narrower type refuses a value that does not fit rather than wrapping
// it. An authored float is a double; decoding into float refuses what float
// cannot hold and rounds only precision. An enum crosses as its EnumSchema
// name, never as its underlying number, so reordering enumerators cannot
// repoint authored content.
//
// A failed decode is a refusal, not a default: the generated adapter answers
// InvalidArguments and the operation never runs on a value nobody sent.
//=============================================================================

template<typename T>
struct AuthoredValueTraits;

template<typename T>
concept IsAuthoredValueType = requires(const AuthoredValue& value, T& out, const T& in,
                                       DataFieldSchema& field) {
    AuthoredValueTraits<T>::Describe(field);
    { AuthoredValueTraits<T>::Decode(value, out) } -> std::same_as<bool>;
    { AuthoredValueTraits<T>::Encode(in) } -> std::same_as<AuthoredValue>;
};

// The integral types whose every value an int64 holds exactly. Character types
// are text, not numbers, and bool is its own kind.
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
    // A type narrower than the authored integer states its own range, so a
    // constant that could never be delivered is refused where it is authored
    // rather than at the first invocation.
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

    // An enumerator the schema does not list has no authored name. It encodes
    // as the absent value, which no enum field accepts, so the mistake is
    // refused at the boundary instead of being sent as some other choice.
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

// Floating-point vectors only. An integer vector would have to narrow the
// authored double components, which is exactly what this contract refuses.
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

// Absent is a value here, and the only type that can say so. A parameter or
// member that must be present is simply not an optional. One level only: an
// absent absent value is not a distinction authored content can make.
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

// The field that holds a present value: the field itself, or the element an
// optional wraps. A range and a target's expected component belong there,
// because that is what a supplied value is checked against.
[[nodiscard]] inline DataFieldSchema& AuthoredValueField(DataFieldSchema& field)
{
    if (field.Kind == DataFieldKind::Optional && field.Children.size() == 1)
        return field.Children.front();
    return field;
}

// Whether a declared range can be delivered to T at all: ordered, and inside
// what T holds. Evaluated by the generated static_asserts, so a range an
// argument could never satisfy fails the build at the declaration.
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

//=============================================================================
// AuthoredSchemaDefault
//
// Which C++ values can be stated as a schema default. Separate from the value
// traits because DataDefaultValue holds only what a JSON document can spell:
// nothing, a bool, an integer, a double or a string. An entity, a tag, an
// asset or a vector has no such spelling, so a C++ default argument of one of
// those types is refused by the generator rather than quietly dropped.
//=============================================================================

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
    // An unlisted enumerator yields an empty name, which schema validation
    // rejects as a default that is not one of the choices.
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

// An optional's default is a value for what it wraps; an empty one is no
// default at all.
template<CanRepresentSchemaDefault T>
struct AuthoredSchemaDefault<std::optional<T>>
{
    static DataDefaultValue ToDefault(const std::optional<T>& value)
    {
        return value ? AuthoredSchemaDefault<T>::ToDefault(*value) : DataDefaultValue{};
    }
};
