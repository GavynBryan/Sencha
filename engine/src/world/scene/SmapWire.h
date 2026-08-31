#pragma once

#include <core/json/JsonValue.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

//=============================================================================
// SmapWire
//
// The byte level the .smap container is built from: how a number, a string, or
// a whole JSON value becomes bytes and comes back, plus the interning table
// that makes a repeated string cost one index and the hash written into the
// file to vouch for it all.
//
// Separate from the container because it knows nothing about one. It has no
// notion of a header, a section directory, an entity record or a dependency
// table -- those are SmapFormat.cpp's, and they change for different reasons
// than this does.
//=============================================================================
namespace SmapWire
{
//-----------------------------------------------------------------------------
// Wire primitives. Everything multi-byte is little-endian, written and read
// through shifts so the format is identical on any host. Unsigned varints are
// LEB128. Numbers are IEEE f64 bit images.
//-----------------------------------------------------------------------------

class ByteBuilder
{
public:
    void U8(std::uint8_t value) { Bytes_.push_back(static_cast<std::byte>(value)); }

    void U32(std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            U8(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    void U64(std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            U8(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void F64(double value) { U64(std::bit_cast<std::uint64_t>(value)); }

    void Varint(std::uint64_t value)
    {
        while (value >= 0x80)
        {
            U8(static_cast<std::uint8_t>(value) | 0x80);
            value >>= 7;
        }
        U8(static_cast<std::uint8_t>(value));
    }

    void Raw(std::string_view text)
    {
        const auto* data = reinterpret_cast<const std::byte*>(text.data());
        Bytes_.insert(Bytes_.end(), data, data + text.size());
    }

    void Append(const ByteBuilder& other)
    {
        Bytes_.insert(Bytes_.end(), other.Bytes_.begin(), other.Bytes_.end());
    }

    // Overwrites bytes written earlier, for the header's content hash: the
    // hash covers the section payloads, which do not exist yet when the
    // header is laid down.
    void PatchU64(std::size_t offset, std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            Bytes_[offset + static_cast<std::size_t>(i)] =
                static_cast<std::byte>(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    [[nodiscard]] std::size_t Size() const { return Bytes_.size(); }
    [[nodiscard]] std::span<const std::byte> View() const { return Bytes_; }
    [[nodiscard]] std::vector<std::byte> Take() { return std::move(Bytes_); }

private:
    std::vector<std::byte> Bytes_;
};

// Bounds-checked cursor over an untrusted byte range. Every read either
// succeeds completely or reports failure; a failed reader never advances past
// the end. Failure carries no message of its own -- the caller names what it
// was reading.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) : Bytes_(bytes) {}

    [[nodiscard]] bool AtEnd() const { return Offset_ == Bytes_.size(); }
    [[nodiscard]] std::size_t Remaining() const { return Bytes_.size() - Offset_; }

    // Guards a reserve() against a hostile or corrupt element count: true when
    // `count` items of at least `minimumSize` bytes each could still follow.
    [[nodiscard]] bool CanHold(std::uint64_t count, std::size_t minimumSize) const
    {
        assert(minimumSize > 0);
        return count <= Remaining() / minimumSize;
    }

    [[nodiscard]] bool U8(std::uint8_t& out)
    {
        if (Offset_ >= Bytes_.size())
            return false;
        out = static_cast<std::uint8_t>(Bytes_[Offset_++]);
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t& out)
    {
        std::uint64_t wide = 0;
        if (!Little(4, wide))
            return false;
        out = static_cast<std::uint32_t>(wide);
        return true;
    }

    [[nodiscard]] bool U64(std::uint64_t& out) { return Little(8, out); }

    [[nodiscard]] bool F32(float& out)
    {
        std::uint32_t bits = 0;
        if (!U32(bits))
            return false;
        out = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool F64(double& out)
    {
        std::uint64_t bits = 0;
        if (!U64(bits))
            return false;
        out = std::bit_cast<double>(bits);
        return true;
    }

    [[nodiscard]] bool Varint(std::uint64_t& out)
    {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            std::uint8_t byte = 0;
            if (!U8(byte))
                return false;
            out |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
                return true;
        }
        return false; // more than 10 continuation bytes: malformed
    }

    [[nodiscard]] bool Text(std::size_t length, std::string& out)
    {
        if (length > Bytes_.size() - Offset_)
            return false;
        out.assign(reinterpret_cast<const char*>(Bytes_.data() + Offset_), length);
        Offset_ += length;
        return true;
    }

private:
    [[nodiscard]] bool Little(std::size_t count, std::uint64_t& out)
    {
        if (count > Bytes_.size() - Offset_)
            return false;
        out = 0;
        for (std::size_t i = 0; i < count; ++i)
            out |= static_cast<std::uint64_t>(
                       static_cast<std::uint8_t>(Bytes_[Offset_ + i]))
                   << (8 * i);
        Offset_ += count;
        return true;
    }

    std::span<const std::byte> Bytes_;
    std::size_t Offset_ = 0;
};

//-----------------------------------------------------------------------------
// Hashing. FNV-1a, the same construction ComponentTypeId uses; the content
// hash runs over the section payloads in directory order so any corruption or
// truncation is caught before a single record is interpreted.
//
// Deliberately local rather than core/hash/Fnv1a.h: these hashes are written
// into cooked files and checked by later builds, and the core header's
// contract explicitly disclaims that stability ("never serialise one"). A
// persisted format owns its hash construction, constants included, so no
// cleanup of the shared helper can quietly invalidate every cooked scene.
//-----------------------------------------------------------------------------

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class Fnv1a
{
public:
    void Byte(std::uint8_t value)
    {
        Hash_ ^= value;
        Hash_ *= kFnvPrime;
    }

    void Bytes(std::span<const std::byte> bytes)
    {
        for (const std::byte b : bytes)
            Byte(static_cast<std::uint8_t>(b));
    }

    void Text(std::string_view text)
    {
        for (const char c : text)
            Byte(static_cast<std::uint8_t>(c));
        Byte(0); // terminator so adjacent strings cannot alias
    }

    void U64(std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            Byte(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    [[nodiscard]] std::uint64_t Value() const { return Hash_; }

private:
    std::uint64_t Hash_ = kFnvOffset;
};

//-----------------------------------------------------------------------------
// String interning. First-encounter order over the writer's fixed traversal
// (dependency paths, then entity component trees, then collision paths), which
// is what makes write-twice byte-identical.
//-----------------------------------------------------------------------------

class StringTableBuilder
{
public:
    std::uint64_t Intern(const std::string& text)
    {
        const auto [it, added] =
            Indices_.try_emplace(text, static_cast<std::uint64_t>(Strings_.size()));
        if (added)
            Strings_.push_back(text);
        return it->second;
    }

    [[nodiscard]] const std::vector<std::string>& Strings() const { return Strings_; }

private:
    std::unordered_map<std::string, std::uint64_t> Indices_;
    std::vector<std::string> Strings_;
};

//-----------------------------------------------------------------------------
// Value codec. A tagged tree mirroring the JSON value set; object keys and
// string values are table indices. Self-delimiting, so component records need
// no length prefix -- the content hash has already vouched for the bytes.
//-----------------------------------------------------------------------------

enum class ValueTag : std::uint8_t
{
    Null = 0,
    False = 1,
    True = 2,
    Number = 3,
    String = 4,
    Array = 5,
    Object = 6,
};

void EncodeValue(const JsonValue& value, StringTableBuilder& strings, ByteBuilder& out)
{
    if (value.IsNull())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Null));
    }
    else if (value.IsBool())
    {
        out.U8(static_cast<std::uint8_t>(value.AsBool() ? ValueTag::True
                                                        : ValueTag::False));
    }
    else if (value.IsNumber())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Number));
        out.F64(value.AsNumber());
    }
    else if (value.IsString())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::String));
        out.Varint(strings.Intern(value.AsString()));
    }
    else if (value.IsArray())
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Array));
        out.Varint(value.AsArray().size());
        for (const JsonValue& element : value.AsArray())
            EncodeValue(element, strings, out);
    }
    else
    {
        out.U8(static_cast<std::uint8_t>(ValueTag::Object));
        out.Varint(value.AsObject().size());
        for (const auto& [key, member] : value.AsObject())
        {
            out.Varint(strings.Intern(key));
            EncodeValue(member, strings, out);
        }
    }
}

// Depth-capped like the JSON parsers, so a crafted file cannot recurse the
// reader off the stack.
constexpr int kMaxValueDepth = 256;

[[nodiscard]] bool DecodeValue(ByteReader& reader,
                               const std::vector<std::string>& strings,
                               JsonValue& out,
                               int depth)
{
    if (depth > kMaxValueDepth)
        return false;

    std::uint8_t tag = 0;
    if (!reader.U8(tag))
        return false;

    switch (static_cast<ValueTag>(tag))
    {
    case ValueTag::Null:
        out = JsonValue();
        return true;
    case ValueTag::False:
        out = JsonValue(false);
        return true;
    case ValueTag::True:
        out = JsonValue(true);
        return true;
    case ValueTag::Number:
    {
        double number = 0.0;
        if (!reader.F64(number))
            return false;
        out = JsonValue(number);
        return true;
    }
    case ValueTag::String:
    {
        std::uint64_t index = 0;
        if (!reader.Varint(index) || index >= strings.size())
            return false;
        out = JsonValue(strings[static_cast<std::size_t>(index)]);
        return true;
    }
    case ValueTag::Array:
    {
        std::uint64_t count = 0;
        if (!reader.Varint(count) || !reader.CanHold(count, 1))
            return false;
        JsonValue::Array array;
        array.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
        {
            JsonValue element;
            if (!DecodeValue(reader, strings, element, depth + 1))
                return false;
            array.push_back(std::move(element));
        }
        out = JsonValue(std::move(array));
        return true;
    }
    case ValueTag::Object:
    {
        std::uint64_t count = 0;
        // at least one key byte + one value byte per member
        if (!reader.Varint(count) || !reader.CanHold(count, 2))
            return false;
        JsonValue::Object object;
        object.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
        {
            std::uint64_t keyIndex = 0;
            if (!reader.Varint(keyIndex) || keyIndex >= strings.size())
                return false;
            JsonValue member;
            if (!DecodeValue(reader, strings, member, depth + 1))
                return false;
            object.emplace_back(strings[static_cast<std::size_t>(keyIndex)],
                                std::move(member));
        }
        out = JsonValue(std::move(object));
        return true;
    }
    }
    return false; // unknown tag
}

} // namespace SmapWire
