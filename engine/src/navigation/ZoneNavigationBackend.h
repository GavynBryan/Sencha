#pragma once

#include <navigation/NavTileMesh.h>
#include <navigation/ZoneNavigation.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class GameplayTagRegistry;

// Private to the navigation module.

// Where one link attaches to one profile's mesh. Polygon 0 at either end means
// the anchor is out of reach, so the link is unusable for that profile.
struct NavLinkEndpoints
{
    NavAnchorAttachment Entry;
    NavAnchorAttachment Exit;

    [[nodiscard]] bool Attached() const { return Entry.Polygon != 0 && Exit.Polygon != 0; }
};

// A directed crossing of link `Link`, leaving polygon `From`: entry to exit
// when Forward, exit to entry otherwise.
struct NavLinkEdge
{
    std::uint64_t From = 0;
    std::uint32_t Link = 0;
    bool Forward = true;
};

struct ZoneNavProfile
{
    GameplayTagId Tag;
    std::string Name;
    NavTileMesh Mesh;
    // Parallel to ZoneNavigation::Backend::Links.
    std::vector<NavLinkEndpoints> Endpoints;
    // Sorted by (From, Link, Forward), for binary search during expansion.
    std::vector<NavLinkEdge> Edges;
    // Each cooked tile's static triangles, sorted by coordinate, for rebuilds.
    std::vector<NavTileRecord> SourceTiles;
    // The cooked tile rectangle. The mesh has capacity for every tile in it, so
    // a rebuild may add a tile anywhere inside and nowhere outside.
    std::optional<NavTileRange> CookedTiles;
};

inline constexpr std::uint8_t kUnboundTraversalKind = 0xff;

struct ZoneNavigation::Backend
{
    ZoneId Zone;
    std::uint32_t Generation = 0;
    // For hierarchical capability matching. The World owns it and outlives every
    // zone resource.
    const GameplayTagRegistry* Tags = nullptr;
    std::vector<ZoneNavProfile> Profiles;
    std::vector<GameplayTagId> AreaTags;
    // Sorted by id.
    std::vector<NavLinkInfo> Links;
    // Distinct bound traversal kinds (at most 64, one capability bit each), and
    // each link's index into them.
    std::vector<GameplayTagId> TraversalKinds;
    std::vector<std::uint8_t> LinkKindIndex;
    // Static source geometry for tile rebuilds.
    std::vector<Vec3d> Positions;
    std::vector<std::uint32_t> Indices;
    float MinY = 0.0f;
    float MaxY = 0.0f;
    std::vector<std::string> Diagnostics;
};

// Re-attaches every link of one profile to its current mesh and rebuilds the
// profile's edge index. Appends to `moved` the links whose attachment changed.
void AttachNavLinks(ZoneNavigation::Backend& zone, ZoneNavProfile& profile,
                    std::vector<std::uint32_t>* moved);

// A profile's cooked record for a tile, or null outside the cooked set.
[[nodiscard]] const NavTileRecord* FindSourceTile(const ZoneNavProfile& profile, NavTileCoord tile);

// Bumps the revision of each listed link.
void BumpNavLinkRevisions(ZoneNavigation::Backend& zone, std::span<const std::uint32_t> links);
