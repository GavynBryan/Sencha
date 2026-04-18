#pragma once

#include <cstdint>
#include <functional>

#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

//=============================================================================
// Identity types
//
// Lightweight, distinct value types for tagging assets, types, and serialized entities.
// Each wraps a uint32_t but is its own type to prevent accidental mixing
// (e.g. passing an AssetId where a SerializedEntityId is expected).
//
// A zero Value means "invalid / unset". The explicit operator bool() lets
// you write: if (id) { ... }
//
// Serialize/Deserialize overloads are provided so these integrate with the
// serialization vocabulary in Serialize.h.
//=============================================================================

// --- AssetId ----------------------------------------------------------------

struct AssetId
{
    std::uint32_t Value = 0;

    bool operator==(const AssetId&) const = default;
    auto operator<=>(const AssetId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

inline bool Serialize(BinaryWriter& writer, const AssetId& id)   { return writer.Write(id.Value); }
inline bool Deserialize(BinaryReader& reader, AssetId& id)       { return reader.Read(id.Value); }

template<> struct std::hash<AssetId>
{
    std::size_t operator()(const AssetId& id) const noexcept
    {
        return std::hash<std::uint32_t>{}(id.Value);
    }
};

// --- TypeId -----------------------------------------------------------------

struct TypeId
{
    std::uint32_t Value = 0;

    bool operator==(const TypeId&) const = default;
    auto operator<=>(const TypeId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

inline bool Serialize(BinaryWriter& writer, const TypeId& id)   { return writer.Write(id.Value); }
inline bool Deserialize(BinaryReader& reader, TypeId& id)       { return reader.Read(id.Value); }

template<> struct std::hash<TypeId>
{
    std::size_t operator()(const TypeId& id) const noexcept
    {
        return std::hash<std::uint32_t>{}(id.Value);
    }
};

// --- SerializedEntityId -----------------------------------------------------

struct SerializedEntityId
{
    std::uint32_t Value = 0;

    bool operator==(const SerializedEntityId&) const = default;
    auto operator<=>(const SerializedEntityId&) const = default;
    explicit operator bool() const { return Value != 0; }
};

inline bool Serialize(BinaryWriter& writer, const SerializedEntityId& id) { return writer.Write(id.Value); }
inline bool Deserialize(BinaryReader& reader, SerializedEntityId& id) { return reader.Read(id.Value); }

template<> struct std::hash<SerializedEntityId>
{
    std::size_t operator()(const SerializedEntityId& id) const noexcept
    {
        return std::hash<std::uint32_t>{}(id.Value);
    }
};
