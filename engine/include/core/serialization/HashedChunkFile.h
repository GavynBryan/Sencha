#pragma once

#include <core/serialization/BinaryFormat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// A BinaryHeader, versioned chunks, then one chunk holding the content hash of
// everything before it.
class HashedChunkFileWriter
{
public:
    HashedChunkFileWriter(std::uint32_t magic, std::uint32_t version);

    [[nodiscard]] BinaryWriter& Writer() { return Bytes; }

    template <typename WritePayload>
    bool Chunk(std::uint32_t id, std::uint32_t version, WritePayload&& writePayload)
    {
        ChunkWriter chunk;
        Ok = Ok && chunk.Begin(Bytes, id, version) && writePayload() && chunk.End(Bytes);
        return Ok;
    }

    // Empty when any write failed.
    [[nodiscard]] std::vector<std::byte> Finish(std::uint32_t hashChunkId);

private:
    std::ostringstream Stream{ std::ios::binary };
    BinaryWriter Bytes{ Stream };
    bool Ok = true;
};

struct HashedChunkFileFormat
{
    std::uint32_t Magic = 0;
    std::uint32_t OldestVersion = 0;
    std::uint32_t Version = 0;
    std::uint32_t HashChunk = 0;
    // "navigation file": names the file in every error.
    std::string_view Subject;
    // Said after a version this build cannot read.
    std::string_view VersionAdvice;
};

// Calls `read` for each chunk before the hash; it reads what it knows of the
// payload, and whatever it leaves is skipped. Fails on a wrong magic or version,
// truncation, a hash that disagrees, or anything after the hash.
[[nodiscard]] bool ReadHashedChunkFile(std::span<const std::byte> bytes, const HashedChunkFileFormat& format,
                                       const std::function<bool(const ChunkHeader&, BinaryReader&)>& read,
                                       std::string* error);
