#include <net/ReplicationCodec.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>

namespace
{
    // Only floats have a range to quantize into. The layout refuses a range on
    // anything else, so this and the switch below are the same question asked
    // once.
    bool IsQuantizedFloat(const ReplicatedField& field)
    {
        return field.Quantization.IsQuantized() && field.Scalar == FieldScalar::Float;
    }

    // Bits a field's one scalar occupies on the wire. Quantized floats take
    // exactly what they declared; everything else takes its natural width.
    //
    // This has to agree with WriteScalar exactly, for every field, or an
    // entity measured to fit a snapshot overflows it while being written -- so
    // both ask IsQuantizedFloat rather than each deciding for itself what
    // counts as quantized.
    std::uint8_t ScalarBits(const ReplicatedField& field)
    {
        if (IsQuantizedFloat(field))
            return field.Quantization.Bits;

        switch (field.Scalar)
        {
        case FieldScalar::Bool:   return 1;
        case FieldScalar::Int32:
        case FieldScalar::UInt32:
        case FieldScalar::Float:  return 32;
        case FieldScalar::UInt64:
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

    // A field's scalar category says how its value travels; field.Size says how
    // many bytes of the component it occupies. Those are different numbers for
    // every integral narrower than four bytes -- all of them travel in the
    // 32-bit category -- so an access sized from the category reads and writes
    // past the field into whatever the component declared next to it. Which is
    // a neighbouring field, and past the last one, whatever the allocator put
    // there.
    //
    // So integers go through these: the category picks the wire width, the
    // declared size picks the bytes. Narrowing on the way in is the value's
    // own truncation and never the neighbour's problem; widening on the way out
    // extends by the category's signedness, so a negative narrow value arrives
    // as itself rather than as a large positive one.
    std::uint32_t LoadUnsigned(const std::byte* at, std::size_t size)
    {
        switch (size)
        {
        case 1: return LoadAs<std::uint8_t>(at);
        case 2: return LoadAs<std::uint16_t>(at);
        case 4: return LoadAs<std::uint32_t>(at);
        default: break;
        }
        // The layout refuses a width this does not implement, so reaching here
        // would mean a table built by something other than ReplicationLayout.
        assert(false && "replicated integer field has no width the codec implements");
        return 0;
    }

    std::int32_t LoadSigned(const std::byte* at, std::size_t size)
    {
        switch (size)
        {
        case 1: return LoadAs<std::int8_t>(at);
        case 2: return LoadAs<std::int16_t>(at);
        case 4: return LoadAs<std::int32_t>(at);
        default: break;
        }
        assert(false && "replicated integer field has no width the codec implements");
        return 0;
    }

    void StoreUnsigned(std::byte* at, std::size_t size, std::uint32_t value)
    {
        switch (size)
        {
        case 1: StoreAs(at, static_cast<std::uint8_t>(value)); return;
        case 2: StoreAs(at, static_cast<std::uint16_t>(value)); return;
        case 4: StoreAs(at, value); return;
        default: break;
        }
        assert(false && "replicated integer field has no width the codec implements");
    }

    void StoreSigned(std::byte* at, std::size_t size, std::int32_t value)
    {
        switch (size)
        {
        case 1: StoreAs(at, static_cast<std::int8_t>(value)); return;
        case 2: StoreAs(at, static_cast<std::int16_t>(value)); return;
        case 4: StoreAs(at, value); return;
        default: break;
        }
        assert(false && "replicated integer field has no width the codec implements");
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
            assert(field.Size == sizeof(bool));
            writer.WriteBool(LoadAs<bool>(at));
            return;
        case FieldScalar::Int32:
            writer.WriteU32(static_cast<std::uint32_t>(LoadSigned(at, field.Size)));
            return;
        case FieldScalar::UInt32:
            writer.WriteU32(LoadUnsigned(at, field.Size));
            return;
        case FieldScalar::UInt64:
            // Full width and no quantization: the values that need this are
            // identities, where every bit is the identity.
            assert(field.Size == sizeof(std::uint64_t));
            writer.WriteU64(LoadAs<std::uint64_t>(at));
            return;
        case FieldScalar::Float:
            assert(field.Size == sizeof(float));
            writer.WriteFloat(LoadAs<float>(at));
            return;
        case FieldScalar::Double:
            assert(field.Size == sizeof(double));
            writer.WriteDouble(LoadAs<double>(at));
            return;
        case FieldScalar::Color3:
        case FieldScalar::Unsupported:
            break;
        }
        assert(false && "replicated field has no encoder");
    }

    std::uint64_t RunMask(std::size_t runs)
    {
        return runs >= 64 ? ~std::uint64_t{ 0 } : (std::uint64_t{ 1 } << runs) - 1;
    }

    // Runs and lists a component actually has. A mask bit past the end would be
    // written as a field the reader has no description of, so it is dropped
    // rather than trusted.
    std::uint64_t AddressableFields(const ReplicatedComponent& component)
    {
        return RunMask(component.Fields.size() + component.Lists.size());
    }

    std::uint64_t ListBit(const ReplicatedComponent& component, std::size_t list)
    {
        return std::uint64_t{ 1 } << (component.Fields.size() + list);
    }

    std::size_t RunBits(const ReplicatedField& field);

    std::size_t RunsBits(std::span<const ReplicatedField> fields, std::uint64_t mask)
    {
        std::size_t bits = 0;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if ((mask & (std::uint64_t{ 1 } << i)) != 0)
                bits += RunBits(fields[i]);
        return bits;
    }

    std::uint64_t VisibleRuns(std::span<const ReplicatedField> fields, bool forOwner)
    {
        std::uint64_t visible = 0;
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            const ReplicatedField& field = fields[i];
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

    void SnapRuns(std::span<const ReplicatedField> fields, std::span<std::byte> bytes);

    std::uint64_t ChangedRuns(std::span<const ReplicatedField> fields, std::span<const std::byte> current,
                              std::span<const std::byte> previous);

    std::uint64_t FoldRuns(std::uint64_t hash, std::span<const ReplicatedField> fields,
                           std::span<const std::byte> bytes, std::uint64_t mask);

    void WriteRuns(std::span<const ReplicatedField> fields, std::span<const std::byte> bytes, std::uint64_t mask,
                   NetBitWriter& writer);

    bool ReadRuns(std::span<const ReplicatedField> fields, std::span<std::byte> bytes, std::uint64_t mask,
                  NetBitReader& reader);

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
            assert(field.Size == sizeof(bool));
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
            StoreSigned(at, field.Size, static_cast<std::int32_t>(value));
            return true;
        }
        case FieldScalar::UInt32:
        {
            std::uint32_t value = 0;
            if (!reader.ReadU32(value))
                return false;
            StoreUnsigned(at, field.Size, value);
            return true;
        }
        case FieldScalar::UInt64:
        {
            assert(field.Size == sizeof(std::uint64_t));
            std::uint64_t value = 0;
            if (!reader.ReadU64(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Float:
        {
            assert(field.Size == sizeof(float));
            float value = 0.0f;
            if (!reader.ReadFloat(value))
                return false;
            StoreAs(at, value);
            return true;
        }
        case FieldScalar::Double:
        {
            assert(field.Size == sizeof(double));
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

namespace
{
    std::size_t RunBits(const ReplicatedField& field)
    {
        return static_cast<std::size_t>(ScalarBits(field)) * field.Count;
    }

    void SnapRuns(std::span<const ReplicatedField> fields, std::span<std::byte> bytes)
    {
        for (const ReplicatedField& field : fields)
        {
            if (!IsQuantizedFloat(field))
                continue;
            for (std::uint8_t i = 0; i < field.Count; ++i)
            {
                std::byte* at = ScalarAt(bytes, field, i);
                const float snapped = ReplicationDequantize(
                    ReplicationQuantize(LoadAs<float>(at), field.Quantization), field.Quantization);
                StoreAs(at, snapped);
            }
        }
    }

    std::uint64_t ChangedRuns(std::span<const ReplicatedField> fields, std::span<const std::byte> current,
                              std::span<const std::byte> previous)
    {
        std::uint64_t changed = 0;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (previous.empty() || FieldDiffers(fields[i], current, previous))
                changed |= (std::uint64_t{ 1 } << i);
        return changed;
    }

    // FNV-1a's constants over a 64-bit accumulator. Chosen for being one line
    // and order-dependent rather than for collision strength: this compares two
    // machines that should agree exactly, so what matters is that a changed byte
    // changes the answer and that both sides fold identically.
    constexpr std::uint64_t kFoldPrime = 1099511628211ull;

    std::uint64_t FoldRuns(std::uint64_t hash, std::span<const ReplicatedField> fields,
                           std::span<const std::byte> bytes, std::uint64_t mask)
    {
        for (std::size_t run = 0; run < fields.size(); ++run)
        {
            if ((mask & (std::uint64_t{ 1 } << run)) == 0)
                continue;
            const ReplicatedField& field = fields[run];

            // The run index folds in as well, so two adjacent runs with swapped
            // values do not hash the same as the pair in the right places.
            hash = (hash ^ static_cast<std::uint64_t>(run)) * kFoldPrime;

            for (std::uint8_t i = 0; i < field.Count; ++i)
            {
                const std::byte* at = ScalarAt(bytes, field, i);
                for (std::size_t byte = 0; byte < field.Size; ++byte)
                    hash = (hash ^ std::to_integer<std::uint64_t>(at[byte])) * kFoldPrime;
            }
        }
        return hash;
    }

    void WriteRuns(std::span<const ReplicatedField> fields, std::span<const std::byte> bytes, std::uint64_t mask,
                   NetBitWriter& writer)
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if ((mask & (std::uint64_t{ 1 } << i)) == 0)
                continue;
            const ReplicatedField& field = fields[i];
            for (std::uint8_t scalar = 0; scalar < field.Count; ++scalar)
                WriteScalar(field, ScalarAt(bytes, field, scalar), writer);
        }
    }

    bool ReadRuns(std::span<const ReplicatedField> fields, std::span<std::byte> bytes, std::uint64_t mask,
                  NetBitReader& reader)
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if ((mask & (std::uint64_t{ 1 } << i)) == 0)
                continue;
            const ReplicatedField& field = fields[i];
            for (std::uint8_t scalar = 0; scalar < field.Count; ++scalar)
                if (!ReadScalar(field, ScalarAt(bytes, field, scalar), reader))
                    return false;
        }
        return true;
    }

    bool ReadMask(std::size_t bits, NetBitReader& reader, std::uint64_t& mask)
    {
        mask = 0;
        for (std::size_t i = 0; i < bits; ++i)
        {
            bool present = false;
            if (!reader.ReadBool(present))
                return false;
            if (present)
                mask |= (std::uint64_t{ 1 } << i);
        }
        return true;
    }

    const std::uint64_t* ElementMasksFor(const ReplicatedComponent& component, const ReplicationImageParts& parts,
                                         std::uint64_t fields, std::span<const std::uint64_t> elements)
    {
        std::size_t needed = 0;
        for (std::size_t list = 0; list < component.Lists.size(); ++list)
            if ((fields & ListBit(component, list)) != 0)
                needed += parts.Lists[list].Count;
        return elements.size() == needed ? elements.data() : nullptr;
    }
}

std::size_t NetVarUIntBits(std::uint64_t value)
{
    std::size_t groups = 1;
    for (std::uint64_t rest = value >> 7; rest != 0; rest >>= 7)
        ++groups;
    return groups * 8;
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

void NetBitWriter::WriteVarUInt(std::uint64_t value)
{
    for (;;)
    {
        const auto group = static_cast<std::uint32_t>(value & 0x7Full);
        value >>= 7;
        WriteBits(group, 7);
        WriteBool(value != 0);
        if (value == 0)
            return;
    }
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

bool NetBitReader::ReadVarUInt(std::uint64_t& out)
{
    out = 0;
    // Ten seven-bit groups is seventy bits, which covers every u64 with room to
    // spare in the last group. An eleventh cannot be describing a u64.
    constexpr std::uint8_t kMaxGroups = 10;
    for (std::uint8_t group = 0; group < kMaxGroups; ++group)
    {
        std::uint32_t payload = 0;
        bool more = false;
        if (!ReadBits(7, payload) || !ReadBool(more))
            return false;
        out |= static_cast<std::uint64_t>(payload) << (group * 7);
        if (!more)
            return true;
    }
    Overflow = true;
    return false;
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
                           std::span<std::byte> image)
{
    SnapRuns(component.Fields, image.subspan(0, component.Size));
    if (component.Lists.empty())
        return;
    ReplicationImageParts parts;
    if (!ReplicationSplitImage(component, image, parts))
    {
        assert(false && "snapping an image that is not well formed");
        return;
    }
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        const ReplicatedList& declared = component.Lists[list];
        for (std::size_t element = 0; element < parts.Lists[list].Count; ++element)
            SnapRuns(declared.Fields, image.subspan(parts.Lists[list].Offset + element * declared.Stride, declared.Stride));
    }
}

std::uint64_t ReplicationFoldVisible(std::uint64_t hash,
                                     const ReplicatedComponent& component,
                                     std::span<const std::byte> image,
                                     bool forOwner)
{
    assert(component.Lists.empty() ? image.size() == component.Size : image.size() >= component.Size);
    hash = FoldRuns(hash, component.Fields, image, VisibleRuns(component.Fields, forOwner));
    if (component.Lists.empty())
        return hash;
    ReplicationImageParts parts;
    if (!ReplicationSplitImage(component, image, parts))
        return hash;
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        const ReplicatedList& declared = component.Lists[list];
        const std::uint64_t visible = VisibleRuns(declared.Fields, forOwner);
        if (visible == 0)
            continue;
        const std::size_t count = parts.Lists[list].Count;
        hash = (hash ^ static_cast<std::uint64_t>(component.Fields.size() + list)) * kFoldPrime;
        hash = (hash ^ static_cast<std::uint64_t>(count)) * kFoldPrime;
        for (std::size_t element = 0; element < count; ++element)
            hash = FoldRuns(hash, declared.Fields,
                            image.subspan(parts.Lists[list].Offset + element * declared.Stride, declared.Stride), visible);
    }
    return hash;
}

//=============================================================================
// Component encode and decode
//=============================================================================

std::size_t ReplicationMaxComponentBits(const ReplicatedComponent& component)
{
    std::size_t bits = component.Fields.size() + component.Lists.size();  // the mask
    bits += RunsBits(component.Fields, ~std::uint64_t{ 0 });
    for (const ReplicatedList& list : component.Lists)
    {
        const std::size_t runMask = list.Fields.size() > 1 ? list.Fields.size() : 0;
        bits += NetVarUIntBits(list.MaxCount)
              + static_cast<std::size_t>(list.MaxCount) * (1 + runMask + RunsBits(list.Fields, ~std::uint64_t{ 0 }));
    }
    return bits;
}

std::uint64_t ReplicationChangedFields(const ReplicatedComponent& component,
                                       std::span<const std::byte> current,
                                       std::span<const std::byte> previous)
{
    assert(current.size() >= component.Size);
    std::uint64_t changed =
        ChangedRuns(component.Fields, current.subspan(0, component.Size),
                    previous.empty() ? previous : previous.subspan(0, component.Size));
    if (component.Lists.empty())
        return changed;

    ReplicationImageParts now;
    ReplicationImageParts before;
    const bool fresh = previous.empty() || !ReplicationSplitImage(component, previous, before);
    if (!ReplicationSplitImage(component, current, now))
        return changed | (AddressableFields(component) & ~RunMask(component.Fields.size()));
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        const ReplicatedList& declared = component.Lists[list];
        const std::size_t count = now.Lists[list].Count;
        bool differs = fresh || count != before.Lists[list].Count;
        for (std::size_t element = 0; !differs && element < count; ++element)
            differs = ChangedRuns(declared.Fields,
                                  current.subspan(now.Lists[list].Offset + element * declared.Stride, declared.Stride),
                                  previous.subspan(before.Lists[list].Offset + element * declared.Stride, declared.Stride))
                   != 0;
        if (differs)
            changed |= ListBit(component, list);
    }
    return changed;
}

std::uint64_t ReplicationChangedElementRuns(const ReplicatedList& list,
                                            std::span<const std::byte> current,
                                            std::span<const std::byte> previous)
{
    return ChangedRuns(list.Fields, current, previous);
}

std::uint64_t ReplicationVisibleFields(const ReplicatedComponent& component,
                                       bool forOwner)
{
    std::uint64_t visible = VisibleRuns(component.Fields, forOwner);
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
        if (VisibleRuns(component.Lists[list].Fields, forOwner) != 0)
            visible |= ListBit(component, list);
    return visible;
}

std::uint64_t ReplicationVisibleElementRuns(const ReplicatedList& list, bool forOwner)
{
    return VisibleRuns(list.Fields, forOwner);
}

bool ReplicationEncodeComponent(const ReplicatedComponent& component,
                                std::span<const std::byte> image,
                                std::uint64_t fields,
                                NetBitWriter& writer,
                                std::span<const std::uint64_t> elements)
{
    assert(component.Lists.empty() ? image.size() == component.Size : image.size() >= component.Size);

    const std::uint64_t mask = fields & AddressableFields(component);
    const std::size_t maskBits = component.Fields.size() + component.Lists.size();
    for (std::size_t i = 0; i < maskBits; ++i)
        writer.WriteBool((mask & (std::uint64_t{ 1 } << i)) != 0);
    WriteRuns(component.Fields, image, mask, writer);

    if ((mask & ~RunMask(component.Fields.size())) == 0)
        return !writer.Overflowed();

    ReplicationImageParts parts;
    if (!ReplicationSplitImage(component, image, parts))
        return false;
    const std::uint64_t* masks = ElementMasksFor(component, parts, mask, elements);
    if (masks == nullptr)
        return false;
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        if ((mask & ListBit(component, list)) == 0)
            continue;
        const ReplicatedList& declared = component.Lists[list];
        const std::uint64_t runs = RunMask(declared.Fields.size());
        const std::size_t count = parts.Lists[list].Count;
        writer.WriteVarUInt(count);
        for (std::size_t element = 0; element < count; ++element)
        {
            const std::uint64_t carried = *masks++ & runs;
            writer.WriteBool(carried != 0);
            if (carried == 0)
                continue;
            if (declared.Fields.size() > 1)
                for (std::size_t run = 0; run < declared.Fields.size(); ++run)
                    writer.WriteBool((carried & (std::uint64_t{ 1 } << run)) != 0);
            WriteRuns(declared.Fields, image.subspan(parts.Lists[list].Offset + element * declared.Stride, declared.Stride),
                      carried, writer);
        }
    }
    return !writer.Overflowed();
}

std::size_t ReplicationEncodedComponentBits(const ReplicatedComponent& component,
                                            std::span<const std::byte> image,
                                            std::uint64_t fields,
                                            std::span<const std::uint64_t> elements)
{
    const std::uint64_t mask = fields & AddressableFields(component);
    std::size_t bits = component.Fields.size() + component.Lists.size();  // the mask
    bits += RunsBits(component.Fields, mask);
    if ((mask & ~RunMask(component.Fields.size())) == 0)
        return bits;

    ReplicationImageParts parts;
    if (!ReplicationSplitImage(component, image, parts))
        return bits;
    const std::uint64_t* masks = ElementMasksFor(component, parts, mask, elements);
    if (masks == nullptr)
        return bits;
    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        if ((mask & ListBit(component, list)) == 0)
            continue;
        const ReplicatedList& declared = component.Lists[list];
        const std::uint64_t runs = RunMask(declared.Fields.size());
        const std::size_t count = parts.Lists[list].Count;
        bits += NetVarUIntBits(count) + count;
        for (std::size_t element = 0; element < count; ++element)
        {
            const std::uint64_t carried = *masks++ & runs;
            if (carried == 0)
                continue;
            bits += (declared.Fields.size() > 1 ? declared.Fields.size() : 0) + RunsBits(declared.Fields, carried);
        }
    }
    return bits;
}

bool ReplicationDecodeComponent(const ReplicatedComponent& component,
                                NetBitReader& reader,
                                std::span<std::byte> target,
                                std::uint64_t* fieldMask)
{
    if (fieldMask != nullptr)
        *fieldMask = 0;
    if (target.size() != component.Size || !component.Lists.empty())
        return false;

    std::uint64_t mask = 0;
    if (!ReadMask(component.Fields.size(), reader, mask) || !ReadRuns(component.Fields, target, mask, reader))
        return false;

    if (fieldMask != nullptr)
        *fieldMask = mask;
    return true;
}

ReplicationDecodeError ReplicationDecodeImage(const ReplicatedComponent& component,
                                              NetBitReader& reader,
                                              ReplicationImage& target,
                                              std::uint64_t* fieldMask)
{
    if (fieldMask != nullptr)
        *fieldMask = 0;

    std::uint64_t mask = 0;
    if (!ReadMask(component.Fields.size() + component.Lists.size(), reader, mask)
        || !ReadRuns(component.Fields, target.Head(), mask, reader))
        return ReplicationDecodeError::Truncated;

    for (std::size_t list = 0; list < component.Lists.size(); ++list)
    {
        if ((mask & ListBit(component, list)) == 0)
            continue;
        const ReplicatedList& declared = component.Lists[list];
        std::uint64_t count = 0;
        if (!reader.ReadVarUInt(count))
            return ReplicationDecodeError::Truncated;
        if (count > declared.MaxCount)
            return ReplicationDecodeError::CountAboveMost;
        const std::span<std::byte> elements = target.ResizeElements(list, static_cast<std::size_t>(count));
        for (std::size_t element = 0; element < count; ++element)
        {
            bool present = false;
            if (!reader.ReadBool(present))
                return ReplicationDecodeError::Truncated;
            if (!present)
                continue;
            std::uint64_t runs = 1;
            if (declared.Fields.size() > 1 && !ReadMask(declared.Fields.size(), reader, runs))
                return ReplicationDecodeError::Truncated;
            if (!ReadRuns(declared.Fields, elements.subspan(element * declared.Stride, declared.Stride), runs, reader))
                return ReplicationDecodeError::Truncated;
        }
    }

    if (fieldMask != nullptr)
        *fieldMask = mask;
    return ReplicationDecodeError::None;
}
