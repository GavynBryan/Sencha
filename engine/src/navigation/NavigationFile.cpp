#include <navigation/NavigationFile.h>

#include <core/hash/ContentHash.h>
#include <core/serialization/BinaryFormat.h>
#include <core/serialization/Serialize.h>
#include <math/MathSchemas.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace
{
    // Bounds on counts read from disk, so a corrupt file cannot demand a huge
    // allocation before its hash is checked.
    constexpr std::uint32_t kMaxRecords = 1u << 24;
    constexpr std::uint32_t kMaxTileBytes = 1u << 28;
    constexpr std::uint32_t kMaxNameLength = 1u << 20;
    constexpr std::uint32_t kMaxProfiles = 256;

    bool DeserializeNavName(BinaryReader& reader, std::string& name)
    {
        return Deserialize(reader, name, kMaxNameLength);
    }

    bool SerializeNavProfile(BinaryWriter& writer, const NavProfileRecord& profile)
    {
        const NavBuildProfile& p = profile.Build;
        return Serialize(writer, profile.Name) && Serialize(writer, p.Radius)
            && Serialize(writer, p.Height) && Serialize(writer, p.MaxSlopeDegrees)
            && Serialize(writer, p.MaxClimb) && Serialize(writer, p.CellSize)
            && Serialize(writer, p.CellHeight) && Serialize(writer, p.TileCells);
    }

    bool DeserializeNavProfile(BinaryReader& reader, NavProfileRecord& profile)
    {
        NavBuildProfile& p = profile.Build;
        return DeserializeNavName(reader, profile.Name) && Deserialize(reader, p.Radius)
            && Deserialize(reader, p.Height) && Deserialize(reader, p.MaxSlopeDegrees)
            && Deserialize(reader, p.MaxClimb) && Deserialize(reader, p.CellSize)
            && Deserialize(reader, p.CellHeight) && Deserialize(reader, p.TileCells);
    }

    bool SerializeNavLink(BinaryWriter& writer, const NavLinkRecord& link)
    {
        return Serialize(writer, link.Id) && Serialize(writer, link.Traversal)
            && Serialize(writer, link.Directions) && Serialize(writer, link.BaseCost)
            && Serialize(writer, link.EntryRadius) && Serialize(writer, link.Entry)
            && Serialize(writer, link.Exit);
    }

    bool DeserializeNavLink(BinaryReader& reader, NavLinkRecord& link)
    {
        return Deserialize(reader, link.Id) && DeserializeNavName(reader, link.Traversal)
            && Deserialize(reader, link.Directions) && Deserialize(reader, link.BaseCost)
            && Deserialize(reader, link.EntryRadius) && Deserialize(reader, link.Entry)
            && Deserialize(reader, link.Exit);
    }

    bool SerializeNavTile(BinaryWriter& writer, const NavTileRecord& tile)
    {
        return Serialize(writer, tile.Coord.X) && Serialize(writer, tile.Coord.Z)
            && SerializeTrivialArray(writer, tile.Data)
            && SerializeTrivialArray(writer, tile.Triangles);
    }

    bool DeserializeNavTile(BinaryReader& reader, NavTileRecord& tile)
    {
        return Deserialize(reader, tile.Coord.X) && Deserialize(reader, tile.Coord.Z)
            && DeserializeTrivialArray(reader, tile.Data, kMaxTileBytes)
            && DeserializeTrivialArray(reader, tile.Triangles, kMaxRecords);
    }

    template <typename WritePayload>
    bool WriteNavChunk(BinaryWriter& writer, std::uint32_t id, WritePayload&& writePayload)
    {
        ChunkWriter chunk;
        return chunk.Begin(writer, id, 1) && writePayload() && chunk.End(writer);
    }

    bool WriteNavBody(BinaryWriter& writer, const NavigationFile& file)
    {
        const bool written =
            WriteBinaryHeader(writer, kNavigationFileMagic, kNavigationFormatVersion)
            && WriteNavChunk(writer, kNavChunkProfiles,
                             [&] { return SerializeArray(writer, file.Profiles, SerializeNavProfile); })
            && WriteNavChunk(writer, kNavChunkAreas,
                             [&] { return SerializeArray<std::string>(writer, file.Areas, Serialize); })
            && WriteNavChunk(writer, kNavChunkGeometry,
                             [&]
                             {
                                 return Serialize(writer, file.MinY) && Serialize(writer, file.MaxY)
                                     && SerializeArray<Vec3d>(writer, file.Positions, Serialize)
                                     && SerializeTrivialArray(writer, file.Indices);
                             })
            && WriteNavChunk(writer, kNavChunkLinks,
                             [&] { return SerializeArray(writer, file.Links, SerializeNavLink); });
        if (!written)
            return false;
        // One tile chunk per profile, each naming the profile it belongs to.
        for (std::uint32_t p = 0; p < file.Profiles.size(); ++p)
        {
            const auto writeTiles = [&]
            {
                return Serialize(writer, p)
                    && SerializeArray(writer, file.Profiles[p].Tiles, SerializeNavTile);
            };
            if (!WriteNavChunk(writer, kNavChunkTiles, writeTiles))
                return false;
        }
        return true;
    }

    bool ReadNavGeometry(BinaryReader& reader, NavigationFile& file)
    {
        if (!Deserialize(reader, file.MinY) || !Deserialize(reader, file.MaxY)
            || !DeserializeArray<Vec3d>(reader, file.Positions, Deserialize, kMaxRecords)
            || !DeserializeTrivialArray(reader, file.Indices, kMaxRecords * 3u)
            || file.Indices.size() % 3 != 0)
            return false;
        return std::ranges::all_of(file.Indices, [&](std::uint32_t index)
                                   { return index < file.Positions.size(); });
    }

    bool ReadNavTiles(BinaryReader& reader, NavigationFile& file)
    {
        std::uint32_t profile = 0;
        return Deserialize(reader, profile) && profile < file.Profiles.size()
            && DeserializeArray(reader, file.Profiles[profile].Tiles, DeserializeNavTile,
                                kMaxRecords);
    }

    // Reads one known chunk's payload; unknown ids are skipped by the caller.
    bool ReadNavChunkPayload(BinaryReader& reader, std::uint32_t id, NavigationFile& file)
    {
        switch (id)
        {
        case kNavChunkProfiles:
            return DeserializeArray(reader, file.Profiles, DeserializeNavProfile, kMaxProfiles);
        case kNavChunkAreas:
            return DeserializeArray(reader, file.Areas, DeserializeNavName,
                                    kNavMaxAuthoredAreas + 1u);
        case kNavChunkGeometry:
            return ReadNavGeometry(reader, file);
        case kNavChunkLinks:
            return DeserializeArray(reader, file.Links, DeserializeNavLink, kMaxRecords);
        case kNavChunkTiles:
            return ReadNavTiles(reader, file);
        default:
            return true;
        }
    }

    bool Fail(std::string* error, const char* message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    }
}

std::vector<std::byte> EncodeNavigationFile(const NavigationFile& file)
{
    std::ostringstream stream(std::ios::binary);
    BinaryWriter writer(stream);
    if (!WriteNavBody(writer, file))
        return {};
    const std::uint64_t hash = HashBytes64(std::string_view(stream.str()));
    if (!WriteNavChunk(writer, kNavChunkHash, [&] { return Serialize(writer, hash); }))
        return {};
    const std::string body = stream.str();
    std::vector<std::byte> bytes(body.size());
    std::memcpy(bytes.data(), body.data(), body.size());
    return bytes;
}

bool DecodeNavigationFile(std::span<const std::byte> bytes, NavigationFile& file,
                          std::string* error)
{
    file = NavigationFile{};
    const std::string body(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(body, std::ios::binary);
    BinaryReader reader(stream);

    BinaryHeader header;
    if (!ReadBinaryHeader(reader, header) || header.Magic != kNavigationFileMagic)
        return Fail(error, "not a navigation file");
    if (header.Version != kNavigationFormatVersion)
        return Fail(error, "navigation file version mismatch (recook the zone)");

    bool hashChecked = false;
    while (!hashChecked && stream.peek() != std::istream::traits_type::eof())
    {
        const std::streampos chunkStart = stream.tellg();
        ChunkReader chunk;
        if (!chunk.ReadHeader(reader))
            return Fail(error, "truncated navigation chunk header");
        const std::uint32_t id = chunk.GetHeader().Id;
        if (id == kNavChunkHash)
        {
            std::uint64_t stored = 0;
            const std::string_view covered(body.data(), static_cast<std::size_t>(chunkStart));
            if (!reader.Read(stored) || stored != HashBytes64(covered))
                return Fail(error, "navigation file hash mismatch");
            hashChecked = true;
        }
        else if (!ReadNavChunkPayload(reader, id, file))
        {
            return Fail(error, "malformed navigation chunk");
        }
        if (!chunk.Skip(reader))
            return Fail(error, "truncated navigation chunk");
    }

    if (!hashChecked)
        return Fail(error, "navigation file has no hash");
    if (stream.peek() != std::istream::traits_type::eof())
        return Fail(error, "data after navigation hash");
    if (file.Profiles.empty())
        return Fail(error, "navigation file has no profiles");
    return true;
}
