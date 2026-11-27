#include <navigation/NavigationFile.h>

#include <core/serialization/BinaryFormat.h>
#include <core/serialization/HashedChunkFile.h>
#include <core/serialization/Serialize.h>
#include <math/MathSchemas.h>

#include <algorithm>

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
            && Serialize(writer, link.Exit) && Serialize(writer, link.Anchored) && Serialize(writer, link.Anchor);
    }

    bool DeserializeNavLink(BinaryReader& reader, NavLinkRecord& link)
    {
        return Deserialize(reader, link.Id) && DeserializeNavName(reader, link.Traversal)
            && Deserialize(reader, link.Directions) && Deserialize(reader, link.BaseCost)
            && Deserialize(reader, link.EntryRadius) && Deserialize(reader, link.Entry)
            && Deserialize(reader, link.Exit) && Deserialize(reader, link.Anchored)
            && Deserialize(reader, link.Anchor);
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

    bool WriteNavBody(HashedChunkFileWriter& file, const NavigationFile& navigation)
    {
        BinaryWriter& writer = file.Writer();
        const bool written =
            file.Chunk(kNavChunkProfiles, 1,
                       [&] { return SerializeArray(writer, navigation.Profiles, SerializeNavProfile); })
            && file.Chunk(kNavChunkAreas, 1,
                             [&] { return SerializeArray<std::string>(writer, navigation.Areas, Serialize); })
            && file.Chunk(kNavChunkGeometry, 1,
                             [&]
                             {
                                 return Serialize(writer, navigation.MinY) && Serialize(writer, navigation.MaxY)
                                     && SerializeArray<Vec3d>(writer, navigation.Positions, Serialize)
                                     && SerializeTrivialArray(writer, navigation.Indices);
                             })
            && file.Chunk(kNavChunkLinks, 1,
                             [&] { return SerializeArray(writer, navigation.Links, SerializeNavLink); });
        if (!written)
            return false;
        // One tile chunk per profile, each naming the profile it belongs to.
        for (std::uint32_t p = 0; p < navigation.Profiles.size(); ++p)
        {
            const auto writeTiles = [&]
            {
                return Serialize(writer, p)
                    && SerializeArray(writer, navigation.Profiles[p].Tiles, SerializeNavTile);
            };
            if (!file.Chunk(kNavChunkTiles, 1, writeTiles))
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

}

std::vector<std::byte> EncodeNavigationFile(const NavigationFile& file)
{
    HashedChunkFileWriter writer(kNavigationFileMagic, kNavigationFormatVersion);
    if (!WriteNavBody(writer, file))
        return {};
    return writer.Finish(kNavChunkHash);
}

bool DecodeNavigationFile(std::span<const std::byte> bytes, NavigationFile& file,
                          std::string* error)
{
    file = NavigationFile{};
    const HashedChunkFileFormat format{ .Magic = kNavigationFileMagic,
                                        .OldestVersion = kNavigationFormatVersion,
                                        .Version = kNavigationFormatVersion,
                                        .HashChunk = kNavChunkHash,
                                        .Subject = "navigation file",
                                        .VersionAdvice = "is not this build's (recook the zone)" };
    const auto read = [&](const ChunkHeader& chunk, BinaryReader& reader) {
        return ReadNavChunkPayload(reader, chunk.Id, file);
    };
    if (!ReadHashedChunkFile(bytes, format, read, error))
        return false;
    if (file.Profiles.empty())
    {
        if (error != nullptr)
            *error = "navigation file has no profiles";
        return false;
    }
    return true;
}
