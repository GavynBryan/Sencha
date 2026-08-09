#include <net/ReplicationCodec.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>

namespace
{
    // Bits a field's one scalar occupies on the wire. Quantized floats take
    // exactly what they declared; everything else takes its natural width.
    std::uint8_t ScalarBits(const ReplicatedField& field)
    {
        if (field.Quantization.IsQuantized())
            return field.Quantization.Bits;

        switch (field.Scalar)
        {
        case FieldScalar::Bool:   return 1;
        case FieldScalar::Int32:
        case FieldScalar::UInt32:
        case FieldScalar::Float:  return 32;
        case FieldScalar::Double: return 64;
        case FieldScalar::Color3:
        case FieldScalar::Unsupported:
            break;
        }
        // The layout refuses these at registration, so reaching here would mean
        // a table built by something other than ReplicationLayout::Add.
        assert(false && "replicated field has no wire width");
        return 0;
    }

    // Only floats have a range to quantize into. A quantized annotation on a
    // non-float is meaningless rather than harmful: the layout keeps it, and
    // the codec ignores it here.
    bool IsQuantizedFloat(const ReplicatedField& field)
    {
        return field.Quantization.IsQuantized() && field.Scalar == FieldScalar::Float;
    }

    const std::byte* ScalarAt(std::span<const std::byte> bytes,
                              const ReplicatedField& field,
                              std::uint8_t index)
    {
        return bytes.data() + field.Offset + static_cast<std::size_t>(index) * field.Size;
    }

    std::byte* ScalarAt(std::span<std::byte> bytes,
                        const ReplicatedField& field,
                        std::uint8_t index)
    {
        return bytes.data() + field.Offset + static_cast<std::size_t>(index) * field.Size;
    }

    template <typename T>
    T LoadAs(const std::byte* at)
    {
        T value{};
        std::memcpy(&value, at, sizeof(T));
        return value;
    }

    template <typename T>
    void StoreAs(std::byte* at, T value)
    {
        std::memcpy(at, &value, sizeof(T));
    }

    // Whether a field differs from the baseline in a way the wire would carry.
    // Quantized floats compare at wire precision, so jitter finer than the
    // declared resolution does not spend a field every tick describing a change
    // the receiver could not represent.
    bool FieldDiffers(const ReplicatedField& field,
                      std::span<const std::byte> current,
                      std::span<const std::byte> baseline)
    {
        for (std::uint8_t i = 0; i < field.Count; ++i)
        {
            const std::byte* a = ScalarAt(current, field, i);
            const std::byte* b = ScalarAt(baseline, field, i);

            if (IsQuantizedFloat(field))
            {
                if (ReplicationQuantize(LoadAs<float>(a), field.Quantization)
                    != ReplicationQuantize(LoadAs<float>(b), field.Quantization))
                    return true;
            }
            else if (std::memcmp(a, b, field.Size) != 0)
            {
                return true;
            }
        }
        return false;
    }

    void WriteScalar(const ReplicatedField& field, const std::byte* at,
                     NetBitWriter& writer)
    {
        if (IsQuantizedFloat(field))
        {
            writer.WriteBits(ReplicationQuantize(LoadAs<float>(at), field.Quantization),
                             field.Quantization.Bits);
            return;
        }

        switch (field.Scalar)
        {
        case FieldScalar::Bool:
            writer.WriteBool(LoadAs<bool>(at));
            return;
        case FieldScalar::Int32:
            writer.WriteU32(static_cast<std::uint32_t>(LoadAs<std::int32_t>(at)));
            return;
        case FieldScalar::UInt32:
            writer.WriteU32(LoadAs<std::uint32_t>(at));
            return;
        case FieldScalar::Float:
            writer.WriteFloat(LoadAs<float>(at));
            return;
        case FieldScalar::Double:
            writer.WriteDouble(LoadAs<double>(at));
            return;
        case FieldScalar::Color3:
        case FieldScalar::Unsupported:
            break;
        }
        assert(false && "replicated field has no encoder");
    }

    // Runs a component actually has. A mask bit past the end would be written as
    // a field the reader has no description of, so it is dropped rather than
    // trusted.
    std::uint64_t AddressableFields(const ReplicatedComponent& component)
    {
        return component.Fields.size() >= 64
                   ? ~std::uint64_t{ 0 }
                   : (std::uint64_t{ 1 } << component.Fields.size()) - 1;
    }

    bool ReadScalar(const ReplicatedField& field, std::byte* at, NetBitReader& reader)
    {
        if (IsQuantizedFloat(field))
        {
            std::uint32_t packed = 0;
            if (!reader.ReadBits(field.Quantization.Bits, packed))
                return false;
            StoreAs(at, ReplicationDequantize(packed, field.Quantization));
            return true;
        }

        switch (field.Scalar)
        {
        case FieldScalar::Bool:
        {
            bool value = false;
            if (!reader.ReadBool(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Int32:
        {
            std::uint32_t value = 0;
            if (!reader.ReadU32(value))
                return false;
            StoreAs(at, static_cast<std::int32_t>(value));
            return true;
        }
        case FieldScalar::UInt32:
        {
            std::uint32_t value = 0;
            if (!reader.ReadU32(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Float:
        {
            float value = 0.0f;
            if (!reader.ReadFloat(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Double:
        {
            double value = 0.0;
            if (!reader.ReadDouble(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Color3:
        case FieldScalar::Unsupported:
            break;
        }
        return false;
    }
}

//=============================================================================
// NetBitWriter
//=============================================================================

void NetBitWriter::WriteBits(std::uint32_t value, std::uint8_t bits)
{
    assert(bits > 0 && bits <= 32 && "WriteBits takes 1..32 bits");
    if (Cursor + bits > Buffer.size() * 8)
    {
        Overflow = true;
        return;
    }

    // Masked rather than trusted: a value wider than its field would otherwise
    // corrupt whatever was written next, which is the sort of bug that only
    // shows up as an unrelated field going wrong.
    if (bits < 32)
        value &= (1u << bits) - 1u;

    for (std::uint8_t written = 0; written < bits; )
    {
        const std::size_t byte = Cursor / 8;
        const std::uint8_t bitInByte = static_cast<std::uint8_t>(Cursor % 8);
        const std::uint8_t room = static_cast<std::uint8_t>(8 - bitInByte);
        const std::uint8_t take = std::min<std::uint8_t>(room, bits - written);

        const std::uint32_t chunk = (value >> written) & ((1u << take) - 1u);
        // Assigning on the first touch of a byte means the caller's scratch
        // buffer needs no clearing between messages: every byte this writer
        // reports as written was fully written by it.
        if (bitInByte == 0)
            Buffer[byte] = static_cast<std::byte>(chunk);
        else
            Buffer[byte] |= static_cast<std::byte>(chunk << bitInByte);

        Cursor += take;
        written = static_cast<std::uint8_t>(written + take);
    }
}

void NetBitWriter::WriteU64(std::uint64_t value)
{
    WriteBits(static_cast<std::uint32_t>(value & 0xFFFFFFFFull), 32);
    WriteBits(static_cast<std::uint32_t>(value >> 32), 32);
}

void NetBitWriter::WriteFloat(float value)
{
    WriteU32(std::bit_cast<std::uint32_t>(value));
}

void NetBitWriter::WriteDouble(double value)
{
    WriteU64(std::bit_cast<std::uint64_t>(value));
}

//=============================================================================
// NetBitReader
//=============================================================================

bool NetBitReader::ReadBits(std::uint8_t bits, std::uint32_t& out)
{
    assert(bits > 0 && bits <= 32 && "ReadBits takes 1..32 bits");
    out = 0;
    if (Cursor + bits > Buffer.size() * 8)
    {
        Overflow = true;
        return false;
    }

    std::uint32_t value = 0;
    for (std::uint8_t read = 0; read < bits; )
    {
        const std::size_t byte = Cursor / 8;
        const std::uint8_t bitInByte = static_cast<std::uint8_t>(Cursor % 8);
        const std::uint8_t room = static_cast<std::uint8_t>(8 - bitInByte);
        const std::uint8_t take = std::min<std::uint8_t>(room, bits - read);

        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(Buffer[byte]) >> bitInByte)
            & ((1u << take) - 1u);
        value |= chunk << read;

        Cursor += take;
        read = static_cast<std::uint8_t>(read + take);
    }

    out = value;
    return true;
}

bool NetBitReader::ReadBool(bool& out)
{
    std::uint32_t value = 0;
    if (!ReadBits(1, value))
        return false;
    out = value != 0;
    return true;
}

bool NetBitReader::ReadU64(std::uint64_t& out)
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!ReadBits(32, low) || !ReadBits(32, high))
        return false;
    out = static_cast<std::uint64_t>(low)
        | (static_cast<std::uint64_t>(high) << 32);
    return true;
}

bool NetBitReader::ReadFloat(float& out)
{
    std::uint32_t bits = 0;
    if (!ReadU32(bits))
        return false;
    out = std::bit_cast<float>(bits);
    return true;
}

bool NetBitReader::ReadDouble(double& out)
{
    std::uint64_t bits = 0;
    if (!ReadU64(bits))
        return false;
    out = std::bit_cast<double>(bits);
    return true;
}

//=============================================================================
// Quantization
//=============================================================================

std::uint32_t ReplicationQuantize(float value, const FieldQuantization& range)
{
    assert(range.IsQuantized() && range.Max > range.Min);

    const std::uint32_t levels = range.Bits >= 32
                                     ? 0xFFFFFFFFu
                                     : (1u << range.Bits) - 1u;

    // NaN fails every comparison, so it is caught here rather than propagating
    // through the arithmetic into an arbitrary integer.
    if (!(value > range.Min))
        return 0;
    if (!(value < range.Max))
        return levels;

    const double span = static_cast<double>(range.Max) - static_cast<double>(range.Min);
    const double scaled =
        (static_cast<double>(value) - static_cast<double>(range.Min)) / span;
    const double level = std::round(scaled * static_cast<double>(levels));
    return static_cast<std::uint32_t>(
        std::clamp(level, 0.0, static_cast<double>(levels)));
}

float ReplicationDequantize(std::uint32_t quantized, const FieldQuantization& range)
{
    assert(range.IsQuantized() && range.Max > range.Min);

    const std::uint32_t levels = range.Bits >= 32
                                     ? 0xFFFFFFFFu
                                     : (1u << range.Bits) - 1u;
    if (quantized >= levels)
        return range.Max;

    const double span = static_cast<double>(range.Max) - static_cast<double>(range.Min);
    const double value = static_cast<double>(range.Min)
                       + (static_cast<double>(quantized) / static_cast<double>(levels))
                             * span;
    return static_cast<float>(value);
}

void ReplicationSnapToWire(const ReplicatedComponent& component,
                           std::span<std::byte> componentBytes)
{
    assert(componentBytes.size() == component.Size);

    for (const ReplicatedField& field : component.Fields)
    {
        if (!IsQuantizedFloat(field))
            continue;
        for (std::uint8_t i = 0; i < field.Count; ++i)
        {
            std::byte* at = ScalarAt(componentBytes, field, i);
            const float snapped = ReplicationDequantize(
                ReplicationQuantize(LoadAs<float>(at), field.Quantization),
                field.Quantization);
            StoreAs(at, snapped);
        }
    }
}

//=============================================================================
// Component encode and decode
//=============================================================================

std::size_t ReplicationMaxComponentBits(const ReplicatedComponent& component)
{
    std::size_t bits = component.Fields.size();  // the mask
    for (const ReplicatedField& field : component.Fields)
        bits += static_cast<std::size_t>(ScalarBits(field)) * field.Count;
    return bits;
}

std::uint64_t ReplicationChangedFields(const ReplicatedComponent& component,
                                       std::span<const std::byte> current,
                                       std::span<const std::byte> previous)
{
    assert(current.size() == component.Size);
    assert(previous.empty() || previous.size() == component.Size);

    std::uint64_t changed = 0;
    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        if (previous.empty() || FieldDiffers(component.Fields[i], current, previous))
            changed |= (std::uint64_t{ 1 } << i);
    }
    return changed;
}

std::uint64_t ReplicationVisibleFields(const ReplicatedComponent& component,
                                       bool forOwner)
{
    std::uint64_t visible = 0;
    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        const ReplicatedField& field = component.Fields[i];
        if (field.OwnerOnly && !forOwner)
            continue;
        // The owner's own answer is newer than anything that could come back
        // to them, so sending it would only overwrite it with a stale one.
        if (field.OwnerLocal && forOwner)
            continue;
        visible |= (std::uint64_t{ 1 } << i);
    }
    return visible;
}

bool ReplicationEncodeComponent(const ReplicatedComponent& component,
                                std::span<const std::byte> current,
                                std::uint64_t fields,
                                NetBitWriter& writer)
{
    assert(current.size() == component.Size);

    const std::uint64_t mask = fields & AddressableFields(component);

    for (std::size_t i = 0; i < component.Fields.size(); ++i)
        writer.WriteBool((mask & (std::uint64_t{ 1 } << i)) != 0);

    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        if ((mask & (std::uint64_t{ 1 } << i)) == 0)
            continue;
        const ReplicatedField& field = component.Fields[i];
        for (std::uint8_t scalar = 0; scalar < field.Count; ++scalar)
            WriteScalar(field, ScalarAt(current, field, scalar), writer);
    }

    return !writer.Overflowed();
}

std::size_t ReplicationEncodedComponentBits(const ReplicatedComponent& component,
                                            std::uint64_t fields)
{
    const std::uint64_t mask = fields & AddressableFields(component);

    std::size_t bits = component.Fields.size();  // the mask
    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        if ((mask & (std::uint64_t{ 1 } << i)) == 0)
            continue;
        const ReplicatedField& field = component.Fields[i];
        bits += static_cast<std::size_t>(ScalarBits(field)) * field.Count;
    }
    return bits;
}

bool ReplicationDecodeComponent(const ReplicatedComponent& component,
                                NetBitReader& reader,
                                std::span<std::byte> target)
{
    if (target.size() != component.Size)
        return false;

    std::uint64_t mask = 0;
    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        bool present = false;
        if (!reader.ReadBool(present))
            return false;
        if (present)
            mask |= (std::uint64_t{ 1 } << i);
    }

    for (std::size_t i = 0; i < component.Fields.size(); ++i)
    {
        if ((mask & (std::uint64_t{ 1 } << i)) == 0)
            continue;
        const ReplicatedField& field = component.Fields[i];
        for (std::uint8_t scalar = 0; scalar < field.Count; ++scalar)
        {
            if (!ReadScalar(field, ScalarAt(target, field, scalar), reader))
                return false;
        }
    }

    return true;
}
