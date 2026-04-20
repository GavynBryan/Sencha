#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/EnumSchema.h>
#include <core/metadata/JsonShape.h>
#include <core/metadata/SchemaVisit.h>
#include <core/metadata/TypeSchema.h>

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

struct IWriteArchive;
struct IReadArchive;

template <typename T>
void WriteArchiveValue(IWriteArchive& archive, std::string_view key, const T& value);

template <typename T>
void ReadArchiveValue(IReadArchive& archive, std::string_view key, T& value);

//=============================================================================
// IWriteArchive
//
// Write-side archive interface. Implementations emit fields into a destination
// format (JSON, binary, etc.). Chain Field/BeginObject/BeginArray/End calls to
// serialize a value; check Ok() after the root call to detect errors.
//=============================================================================
struct IWriteArchive
{
    virtual IWriteArchive& Field(std::string_view key, bool value) = 0;
    virtual IWriteArchive& Field(std::string_view key, float value) = 0;
    virtual IWriteArchive& Field(std::string_view key, double value) = 0;
    virtual IWriteArchive& Field(std::string_view key, std::uint32_t value) = 0;
    virtual IWriteArchive& Field(std::string_view key, std::string_view value) = 0;
    virtual IWriteArchive& BeginObject(std::string_view key) = 0;
    virtual IWriteArchive& BeginArray(std::string_view key, std::size_t count) = 0;
    virtual IWriteArchive& End() = 0;
    virtual bool Ok() const = 0;
    virtual bool IsText() const = 0;
    virtual ~IWriteArchive() = default;

    template <typename FieldT, typename T>
    IWriteArchive& Field(const FieldT& field, const T& value)
    {
        WriteArchiveValue(*this, field.Name, value);
        return *this;
    }
};

//=============================================================================
// IReadArchive
//
// Read-side archive interface. Implementations pull fields from a source format.
// HasField/BeginObject/BeginArray/End navigate the input; MarkMissingField and
// MarkInvalidField record structural errors and set Ok() to false.
//=============================================================================
struct IReadArchive
{
    virtual IReadArchive& Field(std::string_view key, bool& value) = 0;
    virtual IReadArchive& Field(std::string_view key, float& value) = 0;
    virtual IReadArchive& Field(std::string_view key, double& value) = 0;
    virtual IReadArchive& Field(std::string_view key, std::uint32_t& value) = 0;
    virtual IReadArchive& Field(std::string_view key, std::string& value) = 0;
    virtual IReadArchive& BeginObject(std::string_view key) = 0;
    virtual IReadArchive& BeginArray(std::string_view key, std::size_t& count) = 0;
    virtual IReadArchive& End() = 0;
    virtual bool HasField(std::string_view key) const = 0;
    virtual bool Ok() const = 0;
    virtual bool IsText() const = 0;
    virtual void MarkMissingField(std::string_view key) = 0;
    virtual void MarkInvalidField(std::string_view key) = 0;
    virtual ~IReadArchive() = default;

    // Binary archives always return true from HasField, so default/optional handling
    // only triggers for text formats where a key can genuinely be absent.
    template <typename FieldT, typename T>
    IReadArchive& Field(const FieldT& field, T& value)
    {
        if (!HasField(field.Name))
        {
            if (field.DefaultValue)
                value = *field.DefaultValue;
            else if (!field.IsOptional)
                MarkMissingField(field.Name);
            return *this;
        }

        ReadArchiveValue(*this, field.Name, value);
        return *this;
    }

};

template <typename E>
    requires HasEnumSchema<E>
std::string_view EnumToString(E value)
{
    for (const auto& item : EnumSchema<E>::Values)
    {
        if (item.Value == value)
            return item.Name;
    }
    return {};
}

template <typename E>
    requires HasEnumSchema<E>
bool EnumFromString(std::string_view name, E& out)
{
    for (const auto& item : EnumSchema<E>::Values)
    {
        if (item.Name == name)
        {
            out = item.Value;
            return true;
        }
    }
    return false;
}

template <typename T>
void WriteArchiveValue(IWriteArchive& archive, std::string_view key, const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        archive.Field(key, static_cast<std::uint32_t>(value));
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        archive.Field(key, std::string_view(value));
    }
    else if constexpr (std::is_convertible_v<T, std::string_view>)
    {
        archive.Field(key, std::string_view(value));
    }
    else if constexpr (std::is_enum_v<T> && HasEnumSchema<T>)
    {
        if (archive.IsText())
            archive.Field(key, EnumToString(value));
        else
            archive.Field(key, static_cast<std::uint32_t>(value));
    }
    else if constexpr (HasTypeSchema<T>)
    {
        // Pass the compile-time field count as a capacity hint; JSON uses it to
        // pre-size the array, binary ignores it.
        if constexpr (JsonShapeFor<T>::Value == JsonShape::Array)
            archive.BeginArray(key, std::tuple_size_v<decltype(TypeSchema<T>::Fields())>);
        else
            archive.BeginObject(key);

        VisitSchema<TypeSchema<T>>(value, archive);
        archive.End();
    }
    else
    {
        static_assert(sizeof(T) == 0, "No archive serialization available for this type.");
    }
}

template <typename T>
void ReadArchiveValue(IReadArchive& archive, std::string_view key, T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        std::uint32_t temp = 0;
        archive.Field(key, temp);
        value = static_cast<T>(temp);
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        archive.Field(key, value);
    }
    else if constexpr (std::is_enum_v<T> && HasEnumSchema<T>)
    {
        if (archive.IsText())
        {
            std::string name;
            archive.Field(key, name);
            if (archive.Ok() && !EnumFromString(name, value))
                archive.MarkInvalidField(key);
        }
        else
        {
            std::uint32_t temp = 0;
            archive.Field(key, temp);
            value = static_cast<T>(temp);
        }
    }
    else if constexpr (HasTypeSchema<T>)
    {
        std::size_t count = 0;
        if constexpr (JsonShapeFor<T>::Value == JsonShape::Array)
        {
            archive.BeginArray(key, count);
            // Binary returns count=0 (element count not stored); only validate for text.
            if (archive.IsText() && count != std::tuple_size_v<decltype(TypeSchema<T>::Fields())>)
                archive.MarkInvalidField(key);
        }
        else
        {
            archive.BeginObject(key);
        }

        VisitSchema<TypeSchema<T>>(value, archive);
        archive.End();
    }
    else
    {
        static_assert(sizeof(T) == 0, "No archive deserialization available for this type.");
    }
}
