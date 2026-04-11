#include <serialization/BinaryFormat.h>

// --- BinaryHeader -----------------------------------------------------------

bool WriteBinaryHeader(BinaryWriter& writer, std::uint32_t magic, std::uint32_t version)
{
    return writer.Write(magic)
        && writer.Write(version);
}

bool ReadBinaryHeader(BinaryReader& reader, BinaryHeader& header)
{
    return reader.Read(header.Magic)
        && reader.Read(header.Version);
}

bool ValidateBinaryHeader(const BinaryHeader& header,
                           std::uint32_t expectedMagic,
                           std::uint32_t expectedVersion)
{
    return header.Magic == expectedMagic
        && header.Version == expectedVersion;
}

// --- ChunkHeader ------------------------------------------------------------

bool WriteChunkHeader(BinaryWriter& writer, const ChunkHeader& header)
{
    return writer.Write(header.Id)
        && writer.Write(header.Version)
        && writer.Write(header.Size);
}

bool ReadChunkHeader(BinaryReader& reader, ChunkHeader& header)
{
    return reader.Read(header.Id)
        && reader.Read(header.Version)
        && reader.Read(header.Size);
}

// --- ChunkWriter ------------------------------------------------------------

bool ChunkWriter::Begin(BinaryWriter& writer, std::uint32_t id, std::uint32_t version)
{
    if (!writer.Write(id))      return false;
    if (!writer.Write(version)) return false;

    // Remember where the size field is so we can patch it later.
    SizeFieldPos = writer.GetStream().tellp();
    if (SizeFieldPos == std::streampos(-1)) return false;

    std::uint32_t placeholder = 0;
    if (!writer.Write(placeholder)) return false;

    PayloadStartPos = writer.GetStream().tellp();
    return PayloadStartPos != std::streampos(-1);
}

bool ChunkWriter::End(BinaryWriter& writer)
{
    if (SizeFieldPos == std::streampos(-1)) return false;

    auto endPos = writer.GetStream().tellp();
    if (endPos == std::streampos(-1)) return false;

    auto payloadSize = static_cast<std::uint32_t>(endPos - PayloadStartPos);

    // Seek back to the size field and patch it.
    writer.GetStream().seekp(SizeFieldPos);
    if (!writer.Write(payloadSize)) return false;

    // Seek back to the end of the payload.
    writer.GetStream().seekp(endPos);
    return !writer.GetStream().fail();
}

// --- ChunkReader ------------------------------------------------------------

bool ChunkReader::ReadHeader(BinaryReader& reader)
{
    if (!ReadChunkHeader(reader, Header)) return false;

    PayloadStartPos = reader.GetStream().tellg();
    return PayloadStartPos != std::streampos(-1);
}

bool ChunkReader::Skip(BinaryReader& reader)
{
    if (PayloadStartPos == std::streampos(-1)) return false;

    auto targetPos = PayloadStartPos + static_cast<std::streamoff>(Header.Size);
    reader.GetStream().seekg(targetPos);
    return !reader.GetStream().fail();
}
