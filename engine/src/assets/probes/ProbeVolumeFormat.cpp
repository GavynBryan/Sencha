#include <assets/probes/ProbeVolumeFormat.h>

#include <core/serialization/BinaryFormat.h>

#include <bit>
#include <cmath>
#include <cstring>

namespace
{
    struct VolumeTableEntry
    {
        float Origin[3] = {};
        float CellSize = 1.0f;
        std::uint32_t Dims[3] = {};
        std::int32_t Priority = 0;
        std::uint32_t StableIndex = 0;
    };

    bool WriteVolumeTable(BinaryWriter& writer, const ProbeVolumeFile& file)
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, kProbeChunkVolumeTable, 1))
            return false;
        if (!writer.Write(static_cast<std::uint32_t>(file.Volumes.size())))
            return false;
        for (const ProbeVolumeRecord& volume : file.Volumes)
        {
            VolumeTableEntry entry;
            entry.Origin[0] = volume.Grid.Origin.X;
            entry.Origin[1] = volume.Grid.Origin.Y;
            entry.Origin[2] = volume.Grid.Origin.Z;
            entry.CellSize = volume.Grid.CellSize;
            entry.Dims[0] = volume.Grid.DimsX;
            entry.Dims[1] = volume.Grid.DimsY;
            entry.Dims[2] = volume.Grid.DimsZ;
            entry.Priority = volume.Priority;
            entry.StableIndex = volume.StableIndex;
            if (!writer.Write(entry))
                return false;
        }
        return chunk.End(writer);
    }

    bool WriteShChunk(BinaryWriter& writer, std::uint32_t volumeIndex,
                      const ProbeVolumeRecord& volume)
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, kProbeChunkShL1, 1))
            return false;
        if (!writer.Write(volumeIndex))
            return false;
        if (!volume.ShHalf.empty()
            && !writer.WriteBytes(
                reinterpret_cast<const char*>(volume.ShHalf.data()),
                static_cast<std::streamsize>(volume.ShHalf.size() * sizeof(std::uint16_t))))
            return false;
        return chunk.End(writer);
    }

    bool WriteValidityChunk(BinaryWriter& writer, std::uint32_t volumeIndex,
                            const ProbeVolumeRecord& volume)
    {
        ChunkWriter chunk;
        if (!chunk.Begin(writer, kProbeChunkValidity, 1))
            return false;
        if (!writer.Write(volumeIndex))
            return false;
        if (!volume.ValidityBits.empty()
            && !writer.WriteBytes(
                reinterpret_cast<const char*>(volume.ValidityBits.data()),
                static_cast<std::streamsize>(volume.ValidityBits.size())))
            return false;
        return chunk.End(writer);
    }
}

bool WriteProbeVolumeFile(BinaryWriter& writer, const ProbeVolumeFile& file)
{
    if (!WriteBinaryHeader(writer, kProbeFileMagic, kProbeFormatVersion))
        return false;
    if (!WriteVolumeTable(writer, file))
        return false;
    for (std::uint32_t i = 0; i < file.Volumes.size(); ++i)
    {
        if (!WriteShChunk(writer, i, file.Volumes[i]))
            return false;
        if (!file.Volumes[i].ValidityBits.empty()
            && !WriteValidityChunk(writer, i, file.Volumes[i]))
            return false;
    }
    return true;
}

bool ReadProbeVolumeFile(BinaryReader& reader, ProbeVolumeFile& file)
{
    file.Volumes.clear();

    BinaryHeader header;
    if (!ReadBinaryHeader(reader, header)
        || !ValidateBinaryHeader(header, kProbeFileMagic, kProbeFormatVersion))
        return false;

    bool sawVolumeTable = false;
    while (reader.GetStream().peek() != std::istream::traits_type::eof())
    {
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader))
            return false;

        if (chunk.GetHeader().Id == kProbeChunkVolumeTable)
        {
            std::uint32_t count = 0;
            if (!reader.Read(count))
                return false;
            file.Volumes.resize(count);
            for (ProbeVolumeRecord& volume : file.Volumes)
            {
                VolumeTableEntry entry;
                if (!reader.Read(entry))
                    return false;
                volume.Grid.Origin = Vec3d(entry.Origin[0], entry.Origin[1], entry.Origin[2]);
                volume.Grid.CellSize = entry.CellSize;
                volume.Grid.DimsX = entry.Dims[0];
                volume.Grid.DimsY = entry.Dims[1];
                volume.Grid.DimsZ = entry.Dims[2];
                volume.Priority = entry.Priority;
                volume.StableIndex = entry.StableIndex;
            }
            sawVolumeTable = true;
        }
        else if (chunk.GetHeader().Id == kProbeChunkShL1 && sawVolumeTable)
        {
            std::uint32_t volumeIndex = 0;
            if (!reader.Read(volumeIndex) || volumeIndex >= file.Volumes.size())
                return false;
            ProbeVolumeRecord& volume = file.Volumes[volumeIndex];
            const std::size_t halves = static_cast<std::size_t>(volume.Grid.PointCount())
                                     * kProbeShChannels * kProbeShCoefficients;
            volume.ShHalf.resize(halves);
            if (halves > 0
                && !reader.ReadBytes(
                    reinterpret_cast<char*>(volume.ShHalf.data()),
                    static_cast<std::streamsize>(halves * sizeof(std::uint16_t))))
                return false;
        }
        else if (chunk.GetHeader().Id == kProbeChunkValidity && sawVolumeTable)
        {
            std::uint32_t volumeIndex = 0;
            if (!reader.Read(volumeIndex) || volumeIndex >= file.Volumes.size())
                return false;
            ProbeVolumeRecord& volume = file.Volumes[volumeIndex];
            const std::size_t bytes = (volume.Grid.PointCount() + 7u) / 8u;
            volume.ValidityBits.resize(bytes);
            if (bytes > 0
                && !reader.ReadBytes(
                    reinterpret_cast<char*>(volume.ValidityBits.data()),
                    static_cast<std::streamsize>(bytes)))
                return false;
        }

        // Unknown ids, and any trailing payload a newer writer appended to a
        // known chunk, skip forward-compatibly.
        if (!chunk.Skip(reader))
            return false;
    }
    return sawVolumeTable;
}

std::uint16_t FloatToHalf(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent =
        static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x007FFFFFu;

    if (exponent >= 31)
    {
        // Overflow to infinity; NaN keeps a mantissa bit.
        const std::uint32_t nan = ((bits >> 23) & 0xFFu) == 0xFFu && mantissa != 0
                                      ? 0x0200u
                                      : 0u;
        return static_cast<std::uint16_t>(sign | 0x7C00u | nan);
    }
    if (exponent <= 0)
    {
        if (exponent < -10)
            return static_cast<std::uint16_t>(sign);
        // Subnormal: shift the implicit leading bit in, then round.
        mantissa |= 0x00800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        const std::uint32_t rounded = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const std::uint32_t half = 1u << (shift - 1u);
        std::uint32_t result = rounded;
        if (remainder > half || (remainder == half && (rounded & 1u)))
            ++result;
        return static_cast<std::uint16_t>(sign | result);
    }

    std::uint32_t result = (static_cast<std::uint32_t>(exponent) << 10)
                         | (mantissa >> 13);
    const std::uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (result & 1u)))
        ++result;  // carries into the exponent correctly by construction
    return static_cast<std::uint16_t>(sign | result);
}

float HalfToFloat(std::uint16_t value)
{
    const std::uint32_t sign = (static_cast<std::uint32_t>(value) & 0x8000u) << 16;
    const std::uint32_t exponent = (value >> 10) & 0x1Fu;
    const std::uint32_t mantissa = value & 0x03FFu;

    if (exponent == 0)
    {
        if (mantissa == 0)
            return std::bit_cast<float>(sign);
        // Subnormal half: renormalize into a float exponent.
        const int shift = std::countl_zero(mantissa) - 21;
        const std::uint32_t normalized = (mantissa << shift) & 0x03FFu;
        const std::uint32_t exp32 = static_cast<std::uint32_t>(127 - 15 - shift + 1);
        return std::bit_cast<float>(sign | (exp32 << 23) | (normalized << 13));
    }
    if (exponent == 31)
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));

    const std::uint32_t exp32 = exponent - 15 + 127;
    return std::bit_cast<float>(sign | (exp32 << 23) | (mantissa << 13));
}

std::vector<std::uint8_t> PackValidityBits(std::span<const std::uint8_t> flags)
{
    std::vector<std::uint8_t> bits((flags.size() + 7u) / 8u, 0u);
    for (std::size_t i = 0; i < flags.size(); ++i)
        if (flags[i] != 0)
            bits[i / 8u] |= static_cast<std::uint8_t>(1u << (i % 8u));
    return bits;
}

bool ValidityBitSet(std::span<const std::uint8_t> bits, std::uint32_t index)
{
    const std::size_t byte = index / 8u;
    if (byte >= bits.size())
        return false;
    return (bits[byte] & (1u << (index % 8u))) != 0;
}
