#include <core/serialization/HashedChunkFile.h>

#include <core/hash/ContentHash.h>

#include <cstring>

HashedChunkFileWriter::HashedChunkFileWriter(std::uint32_t magic, std::uint32_t version)
{
    Ok = WriteBinaryHeader(Bytes, magic, version);
}

std::vector<std::byte> HashedChunkFileWriter::Finish(std::uint32_t hashChunkId)
{
    const std::uint64_t hash = HashBytes64(std::string_view(Stream.str()));
    if (!Chunk(hashChunkId, 1, [&] { return Bytes.Write(hash); }))
        return {};
    const std::string body = Stream.str();
    std::vector<std::byte> bytes(body.size());
    std::memcpy(bytes.data(), body.data(), body.size());
    return bytes;
}

namespace
{
    HashedChunkFileRead Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return HashedChunkFileRead::Refused;
    }
}

HashedChunkFileRead ReadHashedChunkFile(std::span<const std::byte> bytes, const HashedChunkFileFormat& format,
                         const std::function<bool(const ChunkHeader&, BinaryReader&)>& read, std::string* error)
{
    const std::string subject(format.Subject);
    const std::string body(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(body, std::ios::binary);
    BinaryReader reader(stream);

    BinaryHeader header;
    if (!ReadBinaryHeader(reader, header) || header.Magic != format.Magic)
        return Fail(error, "not a " + subject);
    if (header.Version < format.OldestVersion || header.Version > format.Version)
    {
        const HashedChunkFileRead refused =
            Fail(error, subject + " version " + std::to_string(header.Version) + " " + std::string(format.VersionAdvice));
        return header.Version > format.Version ? HashedChunkFileRead::Newer : refused;
    }

    while (stream.peek() != std::istream::traits_type::eof())
    {
        const std::streampos chunkStart = stream.tellg();
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader))
            return Fail(error, "truncated " + subject + " chunk header");
        if (chunk.GetHeader().Id == format.HashChunk)
        {
            std::uint64_t stored = 0;
            const std::string_view covered(body.data(), static_cast<std::size_t>(chunkStart));
            if (!reader.Read(stored) || stored != HashBytes64(covered))
                return Fail(error, subject + " hash mismatch");
            if (!chunk.Skip(reader) || stream.peek() != std::istream::traits_type::eof())
                return Fail(error, "data after the " + subject + " hash");
            return HashedChunkFileRead::Read;
        }
        if (!read(chunk.GetHeader(), reader))
            return Fail(error, "malformed " + subject + " chunk");
        if (!chunk.Skip(reader))
            return Fail(error, "truncated " + subject + " chunk");
    }
    return Fail(error, subject + " has no hash");
}
