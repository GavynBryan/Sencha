#include <assets/probes/ProbeVolumeFormat.h>

#include <core/serialization/BinaryFormat.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>

namespace
{
    std::stringstream MakeBinaryStream()
    {
        return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
    }

    ProbeVolumeRecord MakeVolume(std::uint32_t stableIndex, std::uint32_t dims)
    {
        ProbeVolumeRecord volume;
        volume.Grid.Origin = Vec3d(1.0f, 2.0f, 3.0f) * static_cast<float>(stableIndex + 1);
        volume.Grid.CellSize = 0.5f + 0.25f * static_cast<float>(stableIndex);
        volume.Grid.DimsX = dims;
        volume.Grid.DimsY = dims;
        volume.Grid.DimsZ = dims;
        volume.Priority = static_cast<std::int32_t>(stableIndex) - 1;
        volume.StableIndex = stableIndex;

        const std::uint32_t halves =
            volume.Grid.PointCount() * kProbeShChannels * kProbeShCoefficients;
        volume.ShHalf.resize(halves);
        for (std::uint32_t i = 0; i < halves; ++i)
            volume.ShHalf[i] = FloatToHalf(0.01f * static_cast<float>(i + stableIndex));

        std::vector<std::uint8_t> flags(volume.Grid.PointCount());
        for (std::uint32_t i = 0; i < flags.size(); ++i)
            flags[i] = (i % 3 == 0) ? 1 : 0;
        volume.ValidityBits = PackValidityBits(flags);
        return volume;
    }
}

TEST(ProbeVolumeFormat, RoundTripsVolumesShAndValidity)
{
    ProbeVolumeFile file;
    file.Volumes.push_back(MakeVolume(0, 2));
    file.Volumes.push_back(MakeVolume(1, 3));

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(WriteProbeVolumeFile(writer, file));

    stream.seekg(0);
    BinaryReader reader(stream);
    ProbeVolumeFile loaded;
    ASSERT_TRUE(ReadProbeVolumeFile(reader, loaded));

    ASSERT_EQ(loaded.Volumes.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i)
    {
        const ProbeVolumeRecord& expected = file.Volumes[i];
        const ProbeVolumeRecord& actual = loaded.Volumes[i];
        EXPECT_FLOAT_EQ(actual.Grid.Origin.X, expected.Grid.Origin.X);
        EXPECT_FLOAT_EQ(actual.Grid.CellSize, expected.Grid.CellSize);
        EXPECT_EQ(actual.Grid.DimsX, expected.Grid.DimsX);
        EXPECT_EQ(actual.Grid.DimsZ, expected.Grid.DimsZ);
        EXPECT_EQ(actual.Priority, expected.Priority);
        EXPECT_EQ(actual.StableIndex, expected.StableIndex);
        EXPECT_EQ(actual.ShHalf, expected.ShHalf);
        EXPECT_EQ(actual.ValidityBits, expected.ValidityBits);
    }
}

TEST(ProbeVolumeFormat, ValidityChunkIsOptional)
{
    ProbeVolumeFile file;
    file.Volumes.push_back(MakeVolume(0, 2));
    file.Volumes[0].ValidityBits.clear();

    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(WriteProbeVolumeFile(writer, file));

    stream.seekg(0);
    BinaryReader reader(stream);
    ProbeVolumeFile loaded;
    ASSERT_TRUE(ReadProbeVolumeFile(reader, loaded));
    ASSERT_EQ(loaded.Volumes.size(), 1u);
    EXPECT_TRUE(loaded.Volumes[0].ValidityBits.empty());
    EXPECT_FALSE(loaded.Volumes[0].ShHalf.empty());
}

TEST(ProbeVolumeFormat, UnknownChunksSkipForwardCompatibly)
{
    ProbeVolumeFile file;
    file.Volumes.push_back(MakeVolume(0, 2));

    // Header + volume table, then a chunk id no reader knows, then the SH
    // payload. A forward-compatible reader skips the stranger and still loads
    // everything else.
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(WriteBinaryHeader(writer, kProbeFileMagic, kProbeFormatVersion));
    // Volume table chunk.
    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, kProbeChunkVolumeTable, 1));
        ASSERT_TRUE(writer.Write(std::uint32_t{ 1 }));
        const ProbeVolumeRecord& v = file.Volumes[0];
        ASSERT_TRUE(writer.Write(v.Grid.Origin.X));
        ASSERT_TRUE(writer.Write(v.Grid.Origin.Y));
        ASSERT_TRUE(writer.Write(v.Grid.Origin.Z));
        ASSERT_TRUE(writer.Write(v.Grid.CellSize));
        ASSERT_TRUE(writer.Write(v.Grid.DimsX));
        ASSERT_TRUE(writer.Write(v.Grid.DimsY));
        ASSERT_TRUE(writer.Write(v.Grid.DimsZ));
        ASSERT_TRUE(writer.Write(v.Priority));
        ASSERT_TRUE(writer.Write(v.StableIndex));
        ASSERT_TRUE(chunk.End(writer));
    }
    // A chunk from the future.
    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, MakeFourCC('W', 'H', 'A', 'T'), 9));
        ASSERT_TRUE(writer.Write(std::uint64_t{ 0xDEADBEEFCAFEBABEull }));
        ASSERT_TRUE(chunk.End(writer));
    }
    // The SH payload.
    {
        ChunkWriter chunk;
        ASSERT_TRUE(chunk.Begin(writer, kProbeChunkShL1, 1));
        ASSERT_TRUE(writer.Write(std::uint32_t{ 0 }));
        ASSERT_TRUE(writer.WriteBytes(
            reinterpret_cast<const char*>(file.Volumes[0].ShHalf.data()),
            static_cast<std::streamsize>(file.Volumes[0].ShHalf.size()
                                         * sizeof(std::uint16_t))));
        ASSERT_TRUE(chunk.End(writer));
    }

    stream.seekg(0);
    BinaryReader reader(stream);
    ProbeVolumeFile loaded;
    ASSERT_TRUE(ReadProbeVolumeFile(reader, loaded));
    ASSERT_EQ(loaded.Volumes.size(), 1u);
    EXPECT_EQ(loaded.Volumes[0].ShHalf, file.Volumes[0].ShHalf);
}

TEST(ProbeVolumeFormat, RejectsWrongMagicAndVersion)
{
    auto stream = MakeBinaryStream();
    BinaryWriter writer(stream);
    ASSERT_TRUE(WriteBinaryHeader(writer, MakeFourCC('N', 'O', 'P', 'E'),
                                  kProbeFormatVersion));
    stream.seekg(0);
    BinaryReader reader(stream);
    ProbeVolumeFile loaded;
    EXPECT_FALSE(ReadProbeVolumeFile(reader, loaded));

    auto versioned = MakeBinaryStream();
    BinaryWriter writer2(versioned);
    ASSERT_TRUE(WriteBinaryHeader(writer2, kProbeFileMagic, kProbeFormatVersion + 1));
    versioned.seekg(0);
    BinaryReader reader2(versioned);
    EXPECT_FALSE(ReadProbeVolumeFile(reader2, loaded));
}

TEST(ProbeVolumeFormat, HalfConversionRoundTripsExactValuesAndRounds)
{
    // Values exactly representable in fp16 survive the round trip bit-exact.
    const float exact[] = { 0.0f, 1.0f, -2.0f, 0.5f, 1024.0f, 0.09997559f };
    for (const float value : exact)
        EXPECT_FLOAT_EQ(HalfToFloat(FloatToHalf(value)), value);

    // fp16 relative precision is about 1/1024; arbitrary values land within it.
    const float arbitrary[] = { 0.1f, 3.14159f, -123.456f, 0.001234f };
    for (const float value : arbitrary)
    {
        const float back = HalfToFloat(FloatToHalf(value));
        EXPECT_NEAR(back, value, std::abs(value) / 900.0f);
    }

    // Determinism: the same input always packs to the same bits.
    EXPECT_EQ(FloatToHalf(0.1f), FloatToHalf(0.1f));

    // Overflow saturates to infinity; sign survives.
    EXPECT_EQ(FloatToHalf(1e6f), 0x7C00u);
    EXPECT_EQ(FloatToHalf(-1e6f), 0xFC00u);
}

TEST(ProbeVolumeFormat, ValidityBitsPackAndQuery)
{
    const std::uint8_t flags[] = { 1, 0, 0, 1, 1, 0, 0, 0, 1, 0 };
    const std::vector<std::uint8_t> bits = PackValidityBits(flags);
    ASSERT_EQ(bits.size(), 2u);
    EXPECT_TRUE(ValidityBitSet(bits, 0));
    EXPECT_FALSE(ValidityBitSet(bits, 1));
    EXPECT_TRUE(ValidityBitSet(bits, 3));
    EXPECT_TRUE(ValidityBitSet(bits, 4));
    EXPECT_TRUE(ValidityBitSet(bits, 8));
    EXPECT_FALSE(ValidityBitSet(bits, 9));
    // Out-of-range queries answer false instead of reading past the span.
    EXPECT_FALSE(ValidityBitSet(bits, 200));
}
