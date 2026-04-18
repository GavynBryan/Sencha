#pragma once

#include <cstdint>

#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

//=============================================================================
// BinaryHeader
//
// Top-level file/stream header. The magic field identifies the format (e.g.
// a FourCC like 'SAVE' or 'ASET'). The version field lets readers reject
// files written by a newer, incompatible writer.
//
// Wire layout (8 bytes):
//   [0..3]  uint32  Magic
//   [4..7]  uint32  Version
//=============================================================================
struct BinaryHeader
{
    std::uint32_t Magic   = 0;
    std::uint32_t Version = 0;
};

[[nodiscard]] bool WriteBinaryHeader(BinaryWriter& writer, std::uint32_t magic, std::uint32_t version);
[[nodiscard]] bool ReadBinaryHeader(BinaryReader& reader, BinaryHeader& header);
[[nodiscard]] bool ValidateBinaryHeader(const BinaryHeader& header, std::uint32_t expectedMagic, std::uint32_t expectedVersion);

//=============================================================================
// ChunkHeader
//
// Describes a tagged, versioned, size-prefixed region within a binary stream.
// Size is the payload byte count (excludes the 12-byte header itself).
//
// Wire layout (12 bytes):
//   [0..3]   uint32  Id
//   [4..7]   uint32  Version
//   [8..11]  uint32  Size  (payload bytes)
//=============================================================================
struct ChunkHeader
{
    std::uint32_t Id      = 0;
    std::uint32_t Version = 0;
    std::uint32_t Size    = 0;
};

[[nodiscard]] bool WriteChunkHeader(BinaryWriter& writer, const ChunkHeader& header);
[[nodiscard]] bool ReadChunkHeader(BinaryReader& reader, ChunkHeader& header);

//=============================================================================
// ChunkWriter
//
// Helper for writing a chunk whose payload size is not known upfront.
// Call Begin() before writing the payload, then End() afterwards to seek
// back and patch the size field. Requires a seekable output stream.
//
// Usage:
//   ChunkWriter chunk;
//   chunk.Begin(writer, chunkId, chunkVersion);
//   // ... write payload fields with writer ...
//   chunk.End(writer);
//=============================================================================
class ChunkWriter
{
public:
    [[nodiscard]] bool Begin(BinaryWriter& writer, std::uint32_t id, std::uint32_t version);
    [[nodiscard]] bool End(BinaryWriter& writer);

private:
    std::streampos SizeFieldPos    = -1;
    std::streampos PayloadStartPos = -1;
};

//=============================================================================
// ChunkReader
//
// Helper for reading chunks. Reads the header, then the caller reads the
// payload fields. Call Skip() to jump past any remaining unread payload
// (useful for forward-compatible readers that encounter unknown chunk
// versions with extra trailing fields).
//
// Usage:
//   ChunkReader chunk;
//   chunk.ReadHeader(reader);
//   if (chunk.GetHeader().Id == expectedId) { /* read payload */ }
//   else { chunk.Skip(reader); }
//=============================================================================
class ChunkReader
{
public:
    [[nodiscard]] bool ReadHeader(BinaryReader& reader);
    [[nodiscard]] bool Skip(BinaryReader& reader);

    const ChunkHeader& GetHeader() const { return Header; }

private:
    ChunkHeader Header{};
    std::streampos PayloadStartPos = -1;
};
