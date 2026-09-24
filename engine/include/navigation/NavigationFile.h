#pragma once

#include <core/serialization/FourCC.h>
#include <math/Vec.h>
#include <navigation/NavTileBuild.h>
#include <navigation/NavigationIds.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// NavigationFile (.snav)
//
// One zone's cooked navigation, self-describing so the runtime needs nothing
// but this file: the build profiles with their parameters, the area names in
// index order, the static collision triangles runtime tile rebuilds start from,
// each profile's tiles, and the authored navigation links.
//
// Layout: BinaryHeader ('SNAV', version) followed by chunks 'PROF', 'AREA',
// 'GEOM', 'LINK', then one 'TILE' chunk per profile, and a final 'HASH' chunk
// holding the XXH64 of every byte before it. A version bump means recook,
// never migrate. Every record is written in canonical order (profiles and
// areas as the settings list them, tiles by (x, z), links by id), so identical
// input produces identical bytes.
//=============================================================================

inline constexpr std::uint32_t kNavigationFileMagic = MakeFourCC('S', 'N', 'A', 'V');
inline constexpr std::uint32_t kNavigationFormatVersion = 1;

inline constexpr std::uint32_t kNavChunkProfiles = MakeFourCC('P', 'R', 'O', 'F');
inline constexpr std::uint32_t kNavChunkAreas = MakeFourCC('A', 'R', 'E', 'A');
inline constexpr std::uint32_t kNavChunkGeometry = MakeFourCC('G', 'E', 'O', 'M');
inline constexpr std::uint32_t kNavChunkLinks = MakeFourCC('L', 'I', 'N', 'K');
inline constexpr std::uint32_t kNavChunkTiles = MakeFourCC('T', 'I', 'L', 'E');
inline constexpr std::uint32_t kNavChunkHash = MakeFourCC('H', 'A', 'S', 'H');

// One tile of one profile. Data is empty when the tile has source triangles but
// no walkable polygon; the record still exists so a runtime rebuild knows which
// static triangles the tile starts from.
struct NavTileRecord
{
    NavTileCoord Coord;
    std::vector<std::byte> Data;
    // Indices of the zone's static triangles overlapping this tile's build
    // bounds, ascending. Triangle t is Indices[3t .. 3t+2] of the geometry.
    std::vector<std::uint32_t> Triangles;
};

struct NavProfileRecord
{
    // Gameplay-tag name the runtime binds, for example
    // "navigation.profile.humanoid".
    std::string Name;
    NavBuildProfile Build;
    std::vector<NavTileRecord> Tiles;
};

struct NavLinkRecord
{
    NavLinkId Id;
    std::string Traversal;
    std::uint32_t Directions = 0;
    float BaseCost = 1.0f;
    float EntryRadius = 0.5f;
    Vec3d Entry = Vec3d::Zero();
    Vec3d Exit = Vec3d::Zero();
};

struct NavigationFile
{
    std::vector<NavProfileRecord> Profiles;
    // Area tag names by backend index. Index 0 is the default area.
    std::vector<std::string> Areas;
    std::vector<Vec3d> Positions;
    std::vector<std::uint32_t> Indices;
    float MinY = 0.0f;
    float MaxY = 0.0f;
    std::vector<NavLinkRecord> Links;
};

[[nodiscard]] std::vector<std::byte> EncodeNavigationFile(const NavigationFile& file);

// Validates the header, version, chunk structure, and trailing hash. Builds no
// navigation objects, so it is safe on a task thread.
[[nodiscard]] bool DecodeNavigationFile(std::span<const std::byte> bytes,
                                        NavigationFile& file,
                                        std::string* error = nullptr);
