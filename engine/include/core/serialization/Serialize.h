#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

//=============================================================================
// Serialize / Deserialize
//
// Free-function vocabulary for binary serialization.
//
// Arithmetic primitives (int, float, bool, etc.) are handled by the generic
// templates below. For record/struct types, provide your own overloads that
// serialize each field explicitly -- do not memcpy entire structs.
//
// Naming conventions used here:
//   Serialize / Deserialize           -- single values and strings
//   SerializeTrivialArray / ...       -- bulk memcpy for trivially-copyable vectors
//   SerializeArray / ...              -- per-element loop with a caller-supplied function
//=============================================================================

// --- Arithmetic primitives (int, float, bool, etc.) -------------------------

template<typename T>
requires std::is_arithmetic_v<T>
bool Serialize(BinaryWriter& writer, const T& value)
{
    return writer.Write(value);
}

template<typename T>
requires std::is_arithmetic_v<T>
bool Deserialize(BinaryReader& reader, T& value)
{
    return reader.Read(value);
}

// --- std::string (uint32 length prefix + raw bytes) -------------------------

[[nodiscard]]
bool Serialize(BinaryWriter& writer, const std::string& value);

[[nodiscard]]
bool Deserialize(BinaryReader& reader, std::string& value,
                 std::uint32_t maxLength = UINT32_MAX);

// --- Trivial array (bulk memcpy for trivially-copyable element types) -------

template<typename T>
requires std::is_trivially_copyable_v<T>
bool SerializeTrivialArray(BinaryWriter& writer, const std::vector<T>& values)
{
    auto count = static_cast<std::uint32_t>(values.size());
    if (!writer.Write(count)) return false;
    if (count == 0) return true;
    return writer.WriteBytes(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(count * sizeof(T)));
}

template<typename T>
requires std::is_trivially_copyable_v<T>
bool DeserializeTrivialArray(BinaryReader& reader, std::vector<T>& values,
                             std::uint32_t maxCount = UINT32_MAX)
{
    std::uint32_t count = 0;
    if (!reader.Read(count)) return false;
    if (count > maxCount) return false;
    values.resize(count);
    if (count == 0) return true;
    return reader.ReadBytes(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(count * sizeof(T)));
}

// --- Per-element array (calls a serialize function for each element) ---------
//
// The caller passes an explicit function pointer so the dispatch is visible
// at the call site. No ADL, no hidden overload resolution.

template<typename T>
bool SerializeArray(BinaryWriter& writer, const std::vector<T>& values,
                    bool(*serializeFn)(BinaryWriter&, const T&))
{
    auto count = static_cast<std::uint32_t>(values.size());
    if (!writer.Write(count)) return false;
    for (const auto& element : values)
    {
        if (!serializeFn(writer, element)) return false;
    }
    return true;
}

template<typename T>
bool DeserializeArray(BinaryReader& reader, std::vector<T>& values,
                      bool(*deserializeFn)(BinaryReader&, T&),
                      std::uint32_t maxCount = UINT32_MAX)
{
    std::uint32_t count = 0;
    if (!reader.Read(count)) return false;
    if (count > maxCount) return false;
    values.resize(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!deserializeFn(reader, values[i])) return false;
    }
    return true;
}
